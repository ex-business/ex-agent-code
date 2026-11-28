#include "workspace_controller.h"
#include "plugins/language_services/language_workspace.h"
#include "plugins/language_services/text_edits.h"
#include "plugins/workspace_viewers/viewer_registry.h"
#include "plugins/workspace_viewers/workspace_preview.h"
#include <QDesktopServices>
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

WorkspaceController::~WorkspaceController() {
    for (const auto &file:m_files) WorkspaceImageProvider::release(file.image.value("url").toUrl());
    for (const auto &view:m_cache) for (const auto &file:view.files) WorkspaceImageProvider::release(file.image.value("url").toUrl());
}
bool WorkspaceController::imageAvailable(const QUrl &url) const { return WorkspaceImageProvider::contains(url); }
void WorkspaceController::openLink(const QString &link,const QString &baseFile) {
    const auto url=QUrl::fromLocalFile(baseFile).resolved(QUrl(link));
    if (url.isLocalFile()) open(url.toLocalFile());
    else if (url.scheme()=="https" || url.scheme()=="http") QDesktopServices::openUrl(url);
    else fail(QStringLiteral("此链接类型不允许打开。"));
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
        for(auto *hub:m_languageHubs)hub->setActive(false);
        m_root = root; m_preview=preview;
        if(!preview && m_languageHubs.contains(root))m_languageHubs[root]->setActive(true);
        const auto cached=m_cache.take(m_root+QString::number(m_preview));
        m_files=cached.files; m_active=cached.active;
        m_expanded.clear();
        m_revealedPath.clear();
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
QString WorkspaceController::regularWorkspaceFile(const QString &relative) const
{
    const auto parts = relative.split('/');
    if (m_root.isEmpty() || QDir::isAbsolutePath(relative) || parts.contains("") || parts.contains(".") ||
        parts.contains("..") || parts.contains(".git", Qt::CaseInsensitive)) return {};
    // Git 和文件树使用字面路径；即使文件名以 file: 开头也不应解析成 URL。
    const auto canonical = resolve(QDir(m_root).absoluteFilePath(relative));
    return !canonical.isEmpty() && QFileInfo(canonical).isFile() ? canonical : QString{};
}

void WorkspaceController::openWorkspaceFile(const QString &relative)
{
    const auto file = regularWorkspaceFile(relative);
    if (file.isEmpty()) { fail(QStringLiteral("当前工作区中无法定位此文件。")); return; }
    open(file);
}

int WorkspaceController::revealFile(const QString &relative)
{
    if (regularWorkspaceFile(relative).isEmpty()) {
        fail(QStringLiteral("当前工作区中无法定位此文件。"));
        return -1;
    }
    const auto parts = relative.split('/');
    // 只临时显示指定文件及其祖先；其余隐藏项仍沿用原树的过滤规则。
    m_revealedPath = relative;
    QStringList prefix;
    for (const auto &part : parts.sliced(0, parts.size()-1)) {
        prefix << part;
        m_expanded.insert(prefix.join('/'));
    }
    refresh();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].toMap().value("path") == relative) { fail({}); return i; }
    }
    fail(QStringLiteral("此文件超出文件树显示范围。"));
    return -1;
}
void WorkspaceController::scan(const QString &directory, int depth, bool revealOnly)
{
    if (depth > 8 || m_entries.size() >= 2000) return;
    const QDir dir(directory);
    const auto excluded = [](const QString &name) {
        return name.startsWith('.') || name == "build" || name == "node_modules";
    };
    QFileInfoList files;
    if (!revealOnly)
        files = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    QString next;
    if (!m_revealedPath.isEmpty()) {
        const auto suffix = dir.relativeFilePath(QDir(m_root).absoluteFilePath(m_revealedPath));
        if (!suffix.startsWith("../") && suffix != "..") next = suffix.section('/', 0, 0);
    }
    // 隐藏或构建目录仅补入定位路径，不扫描其中无关的兄弟文件。
    if (!next.isEmpty() && next != "." && (revealOnly || next.startsWith('.')))
        files.prepend(QFileInfo(dir.filePath(next)));
    for (const auto &file : files) {
        if (m_entries.size() >= 2000) break;
        const bool restricted = excluded(file.fileName());
        if (restricted && file.fileName() != next) continue;
        const auto canonical = resolve(file.absoluteFilePath());
        // 符号链接即使仍在项目内也不递归，防止循环展开；越界链接不展示。
        if (canonical.isEmpty()) continue;
        const auto relative = QDir(m_root).relativeFilePath(file.absoluteFilePath());
        m_entries.append(QVariantMap{{"path", relative}, {"name", file.fileName()}, {"directory", file.isDir()},
                                     {"depth", depth}, {"expanded", m_expanded.contains(relative)}});
        if (file.isDir() && !file.isSymLink() && m_expanded.contains(relative)) scan(file.absoluteFilePath(), depth + 1, revealOnly || restricted);
    }
}
QVariantList WorkspaceController::tabs() const
{
    QVariantList result;
    for (const auto &file : m_files) result.append(QVariantMap{{"path", file.path}, {"name", QFileInfo(file.path).fileName()}, {"state",file.document ? file.document->state() : int(EditorDocument::Clean)}});
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
    const auto suffix=QFileInfo(canonical).suffix().toLower();
    const auto viewer=WorkspaceViewerRegistry::instance().resolve(canonical);
    const bool image=viewer.storage==ViewerDescriptor::Image;
    const bool text=viewer.storage==ViewerDescriptor::Text;
    if (QFileInfo(canonical).size()>viewer.maximumBytes) { fail(QStringLiteral("文件超过此查看器的大小限制。")); return; }
    QVariantMap imageData;
    if (image) { QString error; imageData=WorkspaceImageProvider::registerFile(canonical,m_root,&error); if (imageData.isEmpty()) { fail(error); return; } }
    QByteArray bytes;
    if (text && QFileInfo::exists(canonical)) {
        QFile file(canonical);
        if (file.size()>8*1024*1024 || !file.open(QIODevice::ReadOnly)) { fail(QStringLiteral("文件无法读取或超过 8 MiB 文本限制。")); return; }
        bytes=file.read(8*1024*1024+1);
        if (bytes.size()>8*1024*1024 || bytes.contains('\0')) { fail(QStringLiteral("此文件不支持文本预览。")); return; }
    } else if (text && !EditorDocument::hasDraft(canonical)) { fail(QStringLiteral("文件不存在。")); return; }
    if (m_files.size() >= 8) {
        int clean=-1;
        for (int i=0;i<m_files.size();++i) if (!m_files[i].document || m_files[i].document->state()==EditorDocument::Clean) { clean=i; break; }
        if (clean<0) { WorkspaceImageProvider::release(imageData.value("url").toUrl()); fail(QStringLiteral("八个页签均有未处理修改，请先保存或关闭文件。")); return; }
        closeTab(clean);
    }
    EditorDocument *doc=text ? new EditorDocument(canonical,bytes,m_preview,this) : nullptr;
    if (doc) {
        if(!m_preview){
            auto *hub=m_languageHubs.value(m_root);
            if(!hub){
                hub=new LanguageWorkspace(m_root,this);m_languageHubs.insert(m_root,hub);
                connect(hub,&LanguageWorkspace::openLocation,this,[this,hub](QString path,int line,int character){
                    if(m_preview || m_root!=hub->root())return;open(path);
                    if(auto *target=document();target && target->path()==path){const auto offset=lsp::offset(target->text(),{{"line",line},{"character",character}});if(offset>=0)target->moveCursor(offset,offset);}
                });
                connect(hub,&LanguageWorkspace::documentPrepared,this,[this,hub](EditorDocument *prepared){
                    if(m_preview || m_root!=hub->root())return;
                    const auto relative=QDir(m_root).relativeFilePath(prepared->path());
                    m_documents.insert(prepared);
                    connect(prepared,&EditorDocument::stateChanged,this,[this]{emit previewChanged();});
                    connect(prepared,&EditorDocument::contentChanged,this,[this,prepared]{if(document()==prepared)emit contentChanged();});
                    m_files.append({relative,QFileInfo(prepared->path()).suffix().toLower(),prepared,{},WorkspaceViewerRegistry::instance().resolve(prepared->path()).map()});
                    emit previewChanged();
                });
            }
            hub->add(doc);
        }
        m_documents.insert(doc);
        connect(doc,&EditorDocument::stateChanged,this,[this]{ emit previewChanged(); });
        connect(doc,&EditorDocument::contentChanged,this,[this,doc]{ if (document()==doc) emit contentChanged(); });
    }
    m_files.append({relative,suffix,doc,imageData,viewer.map()});
    m_active = m_files.size() - 1;
    fail({}); emit previewChanged(); emit contentChanged(); if (!m_restoring) emit fileActivated();
}
void WorkspaceController::selectTab(int index) { if (index < 0 || index >= m_files.size()) return; m_active = index; emit previewChanged(); emit contentChanged(); if (!m_restoring) emit fileActivated(); }
void WorkspaceController::closeTab(int index, bool discard)
{
    if (index < 0 || index >= m_files.size()) return;
    auto *doc=m_files[index].document;
    if (doc && doc->editable() && doc->state()!=EditorDocument::Clean && !discard) { emit closeRequested(index); return; }
    if (doc && discard && doc->state()!=EditorDocument::Clean && !doc->discardDraft()) { fail(doc->error()); return; }
    m_documents.remove(doc);
    WorkspaceImageProvider::release(m_files[index].image.value("url").toUrl());
    m_files.removeAt(index);
    if (doc) doc->deleteLater();
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
