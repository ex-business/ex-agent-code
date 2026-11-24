#include "workspace_controller.h"
#include <QCoreApplication>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QSettings>
#include <QCryptographicHash>

namespace exagent {
namespace {
QString tabKey(const QString &path)
{
    return "workspace-tabs/" + QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex();
}
}
WorkspaceController::WorkspaceController(QObject *parent) : QObject(parent)
{
    qApp->installEventFilter(this);
    connect(this, &WorkspaceController::previewChanged, this, [this] {
        if (m_root.isEmpty() || m_restoring) return;
        QStringList paths;
        for (const auto &file : m_files) paths.append(file.path);
        QSettings().setValue(tabKey(m_root), QVariantMap{{"paths",paths},{"active",activePath()}});
    });
}

void WorkspaceController::bindFolder(const QString &path, bool preview)
{
    if (preview && m_previewRoot.isEmpty()) {
        // 只有历史样例沿用开发仓库预览；普通聊天无需在启动时访问开发者目录。
        QDir candidate(QCoreApplication::applicationDirPath());
        for (int level = 0; level < 10; ++level) {
            if (candidate.exists(".git") && candidate.exists("CMakeLists.txt")) {
                m_previewRoot = candidate.canonicalPath();
                break;
            }
            if (!candidate.cdUp()) break;
        }
        if (m_previewRoot.isEmpty()) m_previewRoot = QFileInfo(QCoreApplication::applicationDirPath() + "/../Resources").canonicalFilePath();
    }
    const auto folder = preview ? m_previewRoot : path;
    const auto root = !folder.isEmpty() && QFileInfo(folder).isDir() ? QFileInfo(folder).canonicalFilePath() : QString{};
    if (root != m_root || preview != m_preview) {
        // 切换会话或目录只切换视图，未保存文档和撤销栈继续由宿主管理。
        if (!m_root.isEmpty()) m_cache.insert(m_root+QString::number(m_preview),{m_files,m_active});
        m_root = root; m_preview=preview;
        const auto cached=m_cache.take(m_root+QString::number(m_preview));
        m_files=cached.files; m_active=cached.active;
        m_expanded.clear();
        refresh();
        // 只恢复当前根目录内仍存在的文件；恢复过程不会覆盖尚未加载的页签列表。
        m_restoring = true;
        const auto saved = QSettings().value(tabKey(m_root)).toMap();
        if (!m_root.isEmpty() && m_files.isEmpty()) for (const auto &relative : saved.value("paths").toStringList().mid(0,8)) {
            if (QFileInfo(resolve(relative)).isFile() || EditorDocument::hasDraft(resolve(relative))) open(relative);
        }
        for (int i = 0; i < m_files.size(); ++i)
            if (m_files[i].path == saved.value("active").toString()) m_active = i;
        m_restoring = false;
        emit previewChanged(); emit contentChanged(); emit fileActivated();
    }
    fail(!folder.isEmpty() && root.isEmpty() ? QStringLiteral("工作区目录已移动或不可用。") : QString{});
}
QString WorkspaceController::resolve(const QString &path) const
{
    const QString local = path.startsWith("file:") ? QUrl(path).toLocalFile() : path;
    auto resolved = QFileInfo(QDir(m_root).absoluteFilePath(local)).canonicalFilePath();
    if (resolved.isEmpty() && !m_root.isEmpty()) {
        const auto candidate=QDir::cleanPath(QDir(m_root).absoluteFilePath(local));
        const auto parent=QFileInfo(QFileInfo(candidate).absolutePath()).canonicalFilePath();
        if (!QFileInfo(candidate).exists() && !QFileInfo(candidate).isSymLink() && (parent==m_root || parent.startsWith(m_root+'/')) && EditorDocument::hasDraft(candidate))
            resolved=candidate;
    }
    const auto prefix = m_root.endsWith('/') ? m_root : m_root + '/';
    return !m_root.isEmpty() && (resolved == m_root || resolved.startsWith(prefix)) ? resolved : QString{};
}
void WorkspaceController::refresh() { m_entries.clear(); if (!m_root.isEmpty()) scan(m_root, 0); emit filesChanged(); }
void WorkspaceController::scan(const QString &directory, int depth)
{
    if (depth > 8 || m_entries.size() >= 2000) return;
    const auto files = QDir(directory).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const auto &file : files) {
        if (m_entries.size() >= 2000) break;
        if (file.fileName().startsWith('.') || file.fileName() == "build" || file.fileName() == "node_modules") continue;
        const auto canonical = resolve(file.absoluteFilePath());
        // 符号链接即使仍在项目内也不递归，防止循环展开；越界链接不展示。
        if (canonical.isEmpty()) continue;
        const auto relative = QDir(m_root).relativeFilePath(file.absoluteFilePath());
        m_entries.append(QVariantMap{{"path", relative}, {"name", file.fileName()}, {"directory", file.isDir()},
                                     {"depth", depth}, {"expanded", m_expanded.contains(relative)}});
        if (file.isDir() && !file.isSymLink() && m_expanded.contains(relative)) scan(file.absoluteFilePath(), depth + 1);
    }
}
QVariantList WorkspaceController::tabs() const
{
    QVariantList result;
    for (const auto &file : m_files) result.append(QVariantMap{{"path", file.path}, {"name", QFileInfo(file.path).fileName()}, {"state",file.document->state()}});
    return result;
}
QString WorkspaceController::activePath() const { return m_active >= 0 ? m_files[m_active].path : QString(); }
QString WorkspaceController::source() const { return document() ? document()->text() : QString(); }
QString WorkspaceController::language() const { return m_active >= 0 ? m_files[m_active].language : QString(); }
void WorkspaceController::fail(const QString &message) { m_error = message; emit errorChanged(); }
void WorkspaceController::open(const QString &path)
{
    const auto canonical = resolve(path);
    if (canonical.isEmpty()) { fail(QStringLiteral("只能预览当前工作区内的文件。")); return; }
    const auto relative = QDir(m_root).relativeFilePath(canonical);
    if (QFileInfo(canonical).isDir()) {
        if (m_expanded.contains(relative)) m_expanded.remove(relative); else m_expanded.insert(relative);
        refresh(); return;
    }
    for (int index = 0; index < m_files.size(); ++index) if (m_files[index].path == relative) { selectTab(index); return; }
    QByteArray bytes;
    if (QFileInfo::exists(canonical)) {
        QFile file(canonical);
        if (file.size()>2*1024*1024 || !file.open(QIODevice::ReadOnly)) { fail(QStringLiteral("文件无法读取或超过 2 MiB 预览限制。")); return; }
        bytes=file.read(2*1024*1024+1);
        if (bytes.size()>2*1024*1024 || bytes.contains('\0')) { fail(QStringLiteral("此文件不支持文本预览。")); return; }
    } else if (!EditorDocument::hasDraft(canonical)) { fail(QStringLiteral("文件不存在。")); return; }
    if (m_files.size() >= 8) {
        int clean=-1;
        for (int i=0;i<m_files.size();++i) if (m_files[i].document->state()==EditorDocument::Clean) { clean=i; break; }
        if (clean<0) { fail(QStringLiteral("八个页签均有未处理修改，请先保存或关闭文件。")); return; }
        closeTab(clean);
    }
    auto *doc=new EditorDocument(canonical,bytes,m_preview,this);
    m_documents.insert(doc);
    connect(doc,&EditorDocument::stateChanged,this,[this]{ emit previewChanged(); });
    connect(doc,&EditorDocument::contentChanged,this,[this,doc]{ if (document()==doc) emit contentChanged(); });
    m_files.append({relative,QFileInfo(canonical).suffix().toLower(),doc});
    m_active = m_files.size() - 1;
    fail({}); emit previewChanged(); emit contentChanged(); if (!m_restoring) emit fileActivated();
}
void WorkspaceController::selectTab(int index) { if (index < 0 || index >= m_files.size()) return; m_active = index; emit previewChanged(); emit contentChanged(); if (!m_restoring) emit fileActivated(); }
void WorkspaceController::closeTab(int index, bool discard)
{
    if (index < 0 || index >= m_files.size()) return;
    auto *doc=m_files[index].document;
    if (doc->editable() && doc->state()!=EditorDocument::Clean && !discard) { emit closeRequested(index); return; }
    if (discard && doc->state()!=EditorDocument::Clean && !doc->discardDraft()) { fail(doc->error()); return; }
    m_documents.remove(doc);
    m_files.removeAt(index);
    doc->deleteLater();
    if (m_active > index) --m_active;
    else if (m_active == index) m_active = qMin(index, int(m_files.size()) - 1);
    emit previewChanged(); emit contentChanged();
}
EditorDocument *WorkspaceController::document() const { return m_active>=0 ? m_files[m_active].document : nullptr; }
bool WorkspaceController::hasUnsaved() const {
    for (const auto *doc:m_documents) if (doc->editable() && doc->state()!=EditorDocument::Clean) return true;
    return false;
}
bool WorkspaceController::saveAll() {
    for (auto *doc:m_documents) if (doc->editable() && doc->state()!=EditorDocument::Clean && !doc->save()) { fail(doc->path()+"\n"+doc->error()); return false; }
    fail({}); return true;
}

bool WorkspaceController::flushDrafts() {
    for (auto *doc:m_documents) if (doc->editable() && doc->state()!=EditorDocument::Clean && !doc->flushDraft()) { fail(doc->path()+"\n"+doc->error()); return false; }
    fail({}); return true;
}
bool WorkspaceController::eventFilter(QObject *watched, QEvent *event) {
    if (watched==qApp && event->type()==QEvent::Quit && !m_exitApproved && hasUnsaved()) {
        // 原生菜单退出也走同一确认，写入草稿失败时保留整个应用。
        emit exitRequested(); return true;
    }
    return QObject::eventFilter(watched,event);
}

}
