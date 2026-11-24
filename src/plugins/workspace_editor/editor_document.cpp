#include "editor_document.h"
#include "presentation/code/syntax_highlighter.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#ifdef EX_AGENT_KDE_HIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif
namespace exagent {
namespace {
bool mixedEndings(QByteArray bytes) {
    const bool crlf = bytes.contains("\r\n");
    bytes.replace("\r\n", "");
    return (crlf && (bytes.contains('\n') || bytes.contains('\r'))) ||
           (bytes.contains('\r') && bytes.contains('\n'));
}
QByteArray digest(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
QString draftFor(const QString &path) {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/editor-drafts/" +
           digest(path.toUtf8()) + ".json";
}
class LexicalHighlighter final : public QSyntaxHighlighter {
  public:
    LexicalHighlighter(QTextDocument *document, QString language, bool dark)
        : QSyntaxHighlighter(document), m_language(std::move(language)), m_dark(dark) {}
    void setDark(bool dark) {
        m_dark = dark;
        rehighlight();
    }

  protected:
    void highlightBlock(const QString &text) override {
        const auto result =
            SyntaxHighlighter::scan(text, m_language, qMax(0, previousBlockState()), m_dark, true);
        setCurrentBlockState(result.endState);
        QTextDocument formatted;
        formatted.setHtml(QStringLiteral("<pre>") + result.html + QStringLiteral("</pre>"));
        const auto fragment = formatted.firstBlock();
        for (auto it = fragment.begin(); !it.atEnd(); ++it) {
            const auto run = it.fragment();
            if (run.isValid())
                setFormat(run.position(), run.length(), run.charFormat());
        }
    }

  private:
    QString m_language;
    bool m_dark;
};
} // namespace
EditorDocument::EditorDocument(QString path, QByteArray bytes, bool readOnly, QObject *parent)
    : QObject(parent), m_path(std::move(path)) {
    const auto initialHash = digest(bytes);
    // 显示格式在载入文本之前设置，不进入用户的撤销栈或修改保存点。
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef Q_OS_MACOS
    font.setFamily(QStringLiteral("Menlo"));
#endif
    font.setPixelSize(13);
    m_document.setDefaultFont(font);
    auto option = m_document.defaultTextOption();
    option.setWrapMode(QTextOption::NoWrap);
    option.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    m_document.setDefaultTextOption(option);
    m_document.setDocumentMargin(0);
    if (QFileInfo::exists(m_path))
        m_permissions = QFileInfo(m_path).permissions();
    m_parent = QFileInfo(QFileInfo(m_path).absolutePath()).canonicalFilePath();
    m_bom = bytes.startsWith("\xef\xbb\xbf");
    if (m_bom)
        bytes.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString source = decoder.decode(bytes);
    const bool mixed = mixedEndings(bytes);
    m_expected = initialHash;
    if (bytes.contains("\r\n"))
        m_newline = "\r\n";
    else if (bytes.contains('\r'))
        m_newline = "\r";
    if (readOnly)
        m_reason = QStringLiteral("历史展示工作区只读。");
    else if (decoder.hasError() || mixed || source.contains(QChar::ParagraphSeparator))
        m_reason = QStringLiteral("此编码、换行或段落分隔格式暂仅支持预览。");
    else if (bytes.size() > 512 * 1024)
        m_reason = QStringLiteral("超过 512 KiB 的文件使用只读预览。");
    else if (QFileInfo::exists(m_path) && !QFileInfo(m_path).isWritable())
        m_reason = QStringLiteral("当前文件没有写入权限。");
    if (editable()) {
        const auto directory = QFileInfo(draftPath()).absolutePath();
        if (!QDir().mkpath(directory) ||
            !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
            m_reason = QStringLiteral("恢复草稿目录无法安全写入，当前仅可预览。");
        m_lock = std::make_unique<QLockFile>(draftPath() + ".lock");
        m_lock->setStaleLockTime(0);
        if (m_reason.isEmpty() && !m_lock->tryLock(0))
            m_reason = QStringLiteral("恢复草稿已被另一编辑窗口锁定，当前仅可预览。");
    }
    source.replace("\r\n", "\n");
    source.replace('\r', '\n');
    if (editable())
        m_document.setPlainText(source);
    else
        m_readOnlySource = source;
    m_document.setModified(false);
    QFile draft(draftPath());
    if (editable() && draft.open(QIODevice::ReadOnly) && draft.size() < 8 * 1024 * 1024) {
        const auto saved = QJsonDocument::fromJson(draft.readAll()).object();
        if (saved.value("path").toString() == m_path &&
            (saved.value("state").toInt() == Modified || saved.value("state").toInt() == Conflict)) {
            // 删除后恢复也沿用编辑时的 BOM、换行和权限，不能用空文件默认值重建。
            if (saved.value("bom").isBool())
                m_bom = saved.value("bom").toBool();
            const auto newline = saved.value("newline").toString().toLatin1();
            if (newline == "\n" || newline == "\r\n" || newline == "\r")
                m_newline = newline;
            m_permissions = QFileDevice::Permissions(saved.value("permissions").toInt(int(m_permissions)));
            m_document.setPlainText(saved.value("text").toString());
            m_document.setModified(true);
            m_expected = saved.value("baseline").toString().toLatin1();
            m_conflict = m_expected != initialHash;
        }
    }
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(400);
    m_watchTimer.setSingleShot(true);
    m_watchTimer.setInterval(120);
    connect(&m_draftTimer, &QTimer::timeout, this, &EditorDocument::flushDraft);
    connect(&m_watchTimer, &QTimer::timeout, this, &EditorDocument::checkExternal);
    connect(&m_document, &QTextDocument::contentsChanged, this, [this] {
        if (m_loading)
            return;
        m_draftTimer.start();
        emit contentChanged();
    });
    connect(&m_document, &QTextDocument::undoAvailable, this, [this] { emit historyChanged(); });
    connect(&m_document, &QTextDocument::redoAvailable, this, [this] { emit historyChanged(); });
    connect(&m_document, &QTextDocument::modificationChanged, this, [this] { emit stateChanged(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_watchTimer.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_watchTimer.start(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &EditorDocument::flushDraft);
    watch();
}
EditorDocument::~EditorDocument() {
    flushDraft();
    disconnect(&m_document, nullptr, this, nullptr);
    m_draftTimer.stop();
    m_watchTimer.stop();
    if (m_view && m_view->textDocument() == &m_document)
        m_view->setTextDocument(new QTextDocument(m_view));
    delete m_highlighter;
    m_highlighter = nullptr;
}
void EditorDocument::watch() {
    const auto parent = QFileInfo(m_path).absolutePath();
    if (QFileInfo(parent).isDir() && !m_watcher.directories().contains(parent))
        m_watcher.addPath(parent);
    if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);
}
QByteArray EditorDocument::readDisk(bool *ok) const {
    *ok = false;
    // 同时验证父目录与文件身份，符号链接重定向不能扩大写入范围。
    if (QFileInfo(m_path).canonicalFilePath() != m_path)
        return {};
    QFile file(m_path);
    if (file.size() > 2 * 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(2 * 1024 * 1024 + 1);
    *ok = bytes.size() <= 2 * 1024 * 1024 && file.error() == QFile::NoError;
    return bytes;
}
void EditorDocument::report(const QString &message) {
    m_error = message;
    emit stateChanged();
}
void EditorDocument::checkExternal() {
    watch();
    bool ok = false;
    const auto bytes = readDisk(&ok);
    if (ok && digest(bytes) == m_expected) {
        if (m_conflict) {
            m_conflict = false;
            report({});
        }
        return;
    }
    // 外部变化保留两份内容，用户明确选择后才重新载入或覆盖。
    m_conflict = true;
    report(ok ? QStringLiteral("磁盘文件已变更，请选择重新载入或覆盖保存。")
              : QStringLiteral("文件已移除、不可读取或路径发生变化，编辑内容已保留。"));
    flushDraft();
}
bool EditorDocument::save(bool overwrite) {
    if (!editable()) {
        report(m_reason);
        return false;
    }
    if (QFileInfo::exists(m_path) && !QFileInfo(m_path).isWritable()) {
        report(QStringLiteral("当前文件没有写入权限。"));
        return false;
    }
    bool ok = false;
    const auto disk = readDisk(&ok);
    const bool missing = !QFileInfo::exists(m_path) && !QFileInfo(m_path).isSymLink();
    const bool recreate = overwrite && missing && !m_parent.isEmpty() &&
                          QFileInfo(QFileInfo(m_path).absolutePath()).canonicalFilePath() == m_parent;
    if ((!ok && !recreate) || (!overwrite && digest(disk) != m_expected)) {
        m_conflict = true;
        report(QStringLiteral("磁盘版本已变化，已阻止覆盖。"));
        return false;
    }
    auto bytes = text().toUtf8();
    if (m_newline != "\n")
        bytes.replace("\n", m_newline);
    if (m_bom)
        bytes.prepend("\xef\xbb\xbf");
    if (bytes.size() > 2 * 1024 * 1024 || bytes.contains('\0')) {
        report(QStringLiteral("编辑内容包含空字节或超过 2 MiB 保存上限。"));
        return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        report(QStringLiteral("文件写入失败，原文件已保留。"));
        return false;
    }
    if (recreate && !file.setPermissions(m_permissions)) {
        file.cancelWriting();
        report(QStringLiteral("无法恢复原文件权限，已取消保存。"));
        return false;
    }
    const auto current = readDisk(&ok);
    if ((!recreate && (!ok || current != disk)) ||
        (recreate && (QFileInfo::exists(m_path) || QFileInfo(m_path).isSymLink() ||
                      QFileInfo(QFileInfo(m_path).absolutePath()).canonicalFilePath() != m_parent))) {
        file.cancelWriting();
        m_conflict = true;
        report(QStringLiteral("保存期间磁盘再次变化，已取消写入。"));
        return false;
    }
    if (!file.commit()) {
        report(QStringLiteral("原子保存失败，编辑内容已保留。"));
        return false;
    }
    m_expected = digest(bytes);
    m_conflict = false;
    m_document.setModified(false);
    report({});
    watch();
    flushDraft();
    return true;
}
bool EditorDocument::reload() {
    bool ok = false;
    const auto bytes = readDisk(&ok);
    if (!ok) {
        report(QStringLiteral("无法重新读取文件，编辑内容已保留。"));
        return false;
    }
    auto data = bytes;
    if (data.startsWith("\xef\xbb\xbf"))
        data.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString source = decoder.decode(data);
    if (data.contains('\0') ||
        (editable() && (decoder.hasError() || source.contains(QChar::ParagraphSeparator) ||
                        mixedEndings(data) || data.size() > 512 * 1024))) {
        report(QStringLiteral("磁盘文件已变成不支持的文本编码。"));
        return false;
    }
    m_bom = bytes.startsWith("\xef\xbb\xbf");
    m_newline = data.contains("\r\n") ? "\r\n" : data.contains('\r') ? "\r" : "\n";
    source.replace("\r\n", "\n");
    source.replace('\r', '\n');
    m_loading = true;
    if (editable())
        m_document.setPlainText(source);
    else
        m_readOnlySource = source;
    m_loading = false;
    m_expected = digest(bytes);
    m_conflict = false;
    m_document.setModified(false);
    report({});
    emit contentChanged();
    if (m_highlighter)
        m_highlighter->rehighlight();
    flushDraft();
    return true;
}
void EditorDocument::attach(QQuickTextDocument *view, bool dark) {
    // Qt Quick 的文本控件始终需要有效文档；解绑时交回空文档，编辑文档由插件保留。
    if (!view)
        return;
    const bool changedView = m_view != view || view->textDocument() != &m_document;
    const bool changedTheme = m_dark != dark;
    if (!changedView && !changedTheme && m_highlighter)
        return;
    if (m_view && m_view != view && m_view->textDocument() == &m_document)
        m_view->setTextDocument(new QTextDocument(m_view));
    m_view = view;
    if (view->textDocument() != &m_document)
        view->setTextDocument(&m_document);
    highlight(dark);
    // Qt Quick 换文档会重置布局格式；绑定完成后补一次全量着色，普通输入不走此路径。
    if (m_highlighter)
        m_highlighter->rehighlight();
}
void EditorDocument::highlight(bool dark) {
    if (m_highlighter && m_dark == dark)
        return;
    m_dark = dark;
#ifdef EX_AGENT_KDE_HIGHLIGHTING
    static KSyntaxHighlighting::Repository repository;
    if (!m_highlighter) {
        auto *created = new KSyntaxHighlighting::SyntaxHighlighter(&m_document);
        created->setDefinition(repository.definitionForFileName(m_path));
        m_highlighter = created;
    }
    auto *highlighter = static_cast<KSyntaxHighlighting::SyntaxHighlighter *>(m_highlighter);
    auto theme = repository.theme(dark ? "VSCodium Dark" : "GitHub Light");
    if (!theme.isValid())
        theme = repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme
                                             : KSyntaxHighlighting::Repository::LightTheme);
    highlighter->setTheme(theme);
    m_highlighter = highlighter;
#else
    if (!m_highlighter)
        m_highlighter = new LexicalHighlighter(
            &m_document, SyntaxHighlighter::normalizeLanguage(QFileInfo(m_path).suffix()), dark);
    else
        static_cast<LexicalHighlighter *>(m_highlighter)->setDark(dark);
#endif
}
QString EditorDocument::draftPath() const {
    return draftFor(m_path);
}
bool EditorDocument::flushDraft() {
    m_draftTimer.stop();
    return writeDraft(state());
}
bool EditorDocument::writeDraft(int draftState) {
    if (!editable())
        return true;
    const auto directory = QFileInfo(draftPath()).absolutePath();
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        m_draftError = QStringLiteral("恢复草稿目录不可写，请保存原文件后退出。");
        emit stateChanged();
        return false;
    }
    QSaveFile file(draftPath());
    const auto bytes = QJsonDocument(QJsonObject{{"path", m_path},
                                                 {"baseline", QString::fromLatin1(m_expected)},
                                                 {"state", draftState},
                                                 {"permissions", int(m_permissions)},
                                                 {"bom", m_bom},
                                                 {"newline", QString::fromLatin1(m_newline)},
                                                 {"text", text()}})
                           .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        m_draftError = QStringLiteral("恢复草稿写入失败，请及时保存原文件。");
        emit stateChanged();
        return false;
    }
    if (!m_draftError.isEmpty()) {
        m_draftError.clear();
        emit stateChanged();
    }
    return true;
}
bool EditorDocument::discardDraft() {
    // 先持久化放弃决定，失败时仍保留原缓冲及撤销栈。
    if (!writeDraft(Clean))
        return false;
    m_draftTimer.stop();
    m_conflict = false;
    m_document.setModified(false);
    return true;
}
bool EditorDocument::hasDraft(const QString &path) {
    if (path.isEmpty())
        return false;
    QFile file(draftFor(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return false;
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    const int state = saved.value("state").toInt();
    return saved.value("path").toString() == path && (state == Modified || state == Conflict);
}

} // namespace exagent
