#include "editor_document.h"
#include "editor_page.h"
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
#include <QTextCursor>
#include "plugins/language_services/language_document.h"
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
    // 大文件保留编辑/撤销/保存保护，但不进行逐块语法解析或LSP同步。
    m_large=bytes.size()>512*1024;
    m_expected = initialHash;
    if (bytes.contains("\r\n"))
        m_newline = "\r\n";
    else if (bytes.contains('\r'))
        m_newline = "\r";
    if (readOnly)
        m_reason = QStringLiteral("历史展示工作区只读。");
    else if (decoder.hasError() || mixed || source.contains(QChar::ParagraphSeparator))
        m_reason = QStringLiteral("此编码、换行或段落分隔格式暂仅支持预览。");
    else if (bytes.size() > 8 * 1024 * 1024)
        m_reason = QStringLiteral("超过 8 MiB 的文件暂仅支持专用查看器。");
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
    if(m_large)m_document.setLayoutEnabled(false);
    if (editable())
        m_document.setPlainText(source);
    else {
        m_readOnlySource = source;
        // 只读源码与联动定位也使用同一文档，禁用撤销与写入即可，不能显示空页。
        m_document.setPlainText(source);m_document.setUndoRedoEnabled(false);
    }
    m_document.setModified(false);
    QFile draft(draftPath());
    if (editable() && draft.open(QIODevice::ReadOnly) && draft.size() <= 64 * 1024 * 1024) {
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
    m_large=m_large || m_document.characterCount()>256*1024;
    if(m_large)m_document.setLayoutEnabled(false);
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(m_large ? 2000 : 400);
    m_watchTimer.setSingleShot(true);
    m_watchTimer.setInterval(120);
    connect(&m_draftTimer, &QTimer::timeout, this, &EditorDocument::flushDraft);
    connect(&m_watchTimer, &QTimer::timeout, this, &EditorDocument::checkExternal);
    connect(&m_document, &QTextDocument::contentsChanged, this, [this] {
        if (m_loading)
            return;
        if(!m_large && m_document.characterCount()>256*1024)QTimer::singleShot(0,this,[this]{if(m_document.characterCount()>256*1024)enterLargeMode();});
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
    delete m_language; m_language=nullptr;
    delete m_page; m_page=nullptr;
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
    if (file.size() > 8 * 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(8 * 1024 * 1024 + 1);
    *ok = bytes.size() <= 8 * 1024 * 1024 && file.error() == QFile::NoError;
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
    if (bytes.size() > 8 * 1024 * 1024 || bytes.contains('\0')) {
        report(QStringLiteral("编辑内容包含空字节或超过 8 MiB 保存上限。"));
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
    emit saved();
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
                        mixedEndings(data) || data.size() > 8 * 1024 * 1024))) {
        report(QStringLiteral("磁盘文件已变成不支持的文本编码。"));
        return false;
    }
    m_bom = bytes.startsWith("\xef\xbb\xbf");
    m_newline = data.contains("\r\n") ? "\r\n" : data.contains('\r') ? "\r" : "\n";
    source.replace("\r\n", "\n");
    source.replace('\r', '\n');
    if(data.size()>512*1024 || source.size()>256*1024)enterLargeMode();
    m_loading = true;
    if (editable())
        m_document.setPlainText(source);
    else {
        m_readOnlySource = source;
        // 只读源码与联动定位也使用同一文档，禁用撤销与写入即可，不能显示空页。
        m_document.setPlainText(source);m_document.setUndoRedoEnabled(false);
    }
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
    if (!view || m_large)
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
void EditorDocument::enterLargeMode() {
    if(m_large)return;
    m_large=true;const bool loading=m_loading;m_loading=true;
    if(m_view && m_view->textDocument()==&m_document){m_view->setTextDocument(new QTextDocument(m_view));m_view=nullptr;}
    m_document.setLayoutEnabled(false);
    if(m_highlighter){m_highlighter->setDocument(nullptr);m_highlighter->deleteLater();m_highlighter=nullptr;}
    m_loading=loading;m_draftTimer.setInterval(2000);emit modeChanged();
}
void EditorDocument::highlight(bool dark) {
    if(m_large){m_dark=dark;return;}
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
bool EditorDocument::applyTextEdits(const QString &expected,const QVector<lsp::Edit> &edits) {
    if(!editable() || state()==Conflict || text()!=expected){report(QStringLiteral("正文已变化或不可编辑，未应用语言服务修改。"));return false;}
    QString next=expected;
    for(auto it=edits.crbegin();it!=edits.crend();++it)next.replace(it->start,it->end-it->start,it->text);
    if(next.toUtf8().size()>8*1024*1024){report(QStringLiteral("语言服务修改超过8MiB保存上限。"));return false;}
    QTextCursor cursor(&m_document);cursor.beginEditBlock();
    for(auto it=edits.crbegin();it!=edits.crend();++it){cursor.setPosition(it->start);cursor.setPosition(it->end,QTextCursor::KeepAnchor);cursor.insertText(it->text);}
    cursor.endEditBlock();return true;
}
EditorPage *EditorDocument::page(){if(!m_page)m_page=new EditorPage(this);return m_page;}
int EditorDocument::lineOffset(int line)const{return m_document.findBlockByNumber(line).position();}
int EditorDocument::lineAt(int at)const{return m_document.findBlock(qBound(0,at,characterCount())).blockNumber();}
bool EditorDocument::lowSurrogateAt(int at)const{return at>0 && at<characterCount() && m_document.characterAt(at).isLowSurrogate() && m_document.characterAt(at-1).isHighSurrogate();}
QString EditorDocument::rangeText(int start,int end)const{QTextCursor cursor(const_cast<QTextDocument*>(&m_document));cursor.setPosition(qBound(0,start,characterCount()));cursor.setPosition(qBound(0,end,characterCount()),QTextCursor::KeepAnchor);return cursor.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));}
bool EditorDocument::replaceWindow(int start,const QString &before,const QString &after){
    if(!editable() || rangeText(start,start+before.size())!=before)return false;
    int prefix=0,suffix=0;while(prefix<before.size() && prefix<after.size() && before[prefix]==after[prefix])++prefix;
    if(prefix>0 && prefix<before.size() && before[prefix].isLowSurrogate())--prefix;
    while(suffix<before.size()-prefix && suffix<after.size()-prefix && before[before.size()-suffix-1]==after[after.size()-suffix-1])++suffix;
    if(suffix>0 && suffix<before.size() && before[before.size()-suffix].isLowSurrogate())--suffix;
    // 只把本次差量放进完整文档撤销块，不为每个按键复制整页到撤销历史。
    QTextCursor cursor(&m_document);cursor.beginEditBlock();cursor.setPosition(start+prefix);cursor.setPosition(start+before.size()-suffix,QTextCursor::KeepAnchor);cursor.insertText(after.mid(prefix,after.size()-prefix-suffix));cursor.endEditBlock();return true;
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
    if (bytes.size()>64*1024*1024) { m_draftError=QStringLiteral("恢复草稿超过64MiB，请直接保存原文件。");emit stateChanged();return false; }
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
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        return false;
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    const int state = saved.value("state").toInt();
    return saved.value("path").toString() == path && (state == Modified || state == Conflict);
}

} // namespace exagent
