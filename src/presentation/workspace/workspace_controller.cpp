#include "workspace_controller.h"
#include <QCoreApplication>
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
    if (root != m_root) {
        // 标签页和已展开的相对路径属于之前的根目录，换目录时一并切换。
        m_root = root;
        m_files.clear();
        m_expanded.clear();
        m_active = -1;
        refresh();
        // 只恢复当前根目录内仍存在的文件；恢复过程不会覆盖尚未加载的页签列表。
        m_restoring = true;
        const auto saved = QSettings().value(tabKey(m_root)).toMap();
        if (!m_root.isEmpty()) for (const auto &relative : saved.value("paths").toStringList().mid(0,8)) {
            if (QFileInfo(resolve(relative)).isFile()) open(relative);
        }
        for (int i = 0; i < m_files.size(); ++i)
            if (m_files[i].path == saved.value("active").toString()) m_active = i;
        m_restoring = false;
        emit previewChanged();
    }
    fail(!folder.isEmpty() && root.isEmpty() ? QStringLiteral("工作区目录已移动或不可用。") : QString{});
}
QString WorkspaceController::resolve(const QString &path) const
{
    const QString local = path.startsWith("file:") ? QUrl(path).toLocalFile() : path;
    const auto resolved = QFileInfo(QDir(m_root).absoluteFilePath(local)).canonicalFilePath();
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
    for (const auto &file : m_files) result.append(QVariantMap{{"path", file.path}, {"name", QFileInfo(file.path).fileName()}});
    return result;
}
QString WorkspaceController::activePath() const { return m_active >= 0 ? m_files[m_active].path : QString(); }
QString WorkspaceController::source() const { return m_active >= 0 ? m_files[m_active].source : QString(); }
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
    QFile file(canonical);
    // 预览有明确大小上限，不读取二进制，也不把文件内容交给外部服务。
    if (file.size() > 2 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) { fail(QStringLiteral("文件无法读取或超过 2 MiB 预览限制。")); return; }
    const auto bytes = file.read(2 * 1024 * 1024 + 1);
    if (bytes.size() > 2 * 1024 * 1024 || bytes.contains('\0')) { fail(QStringLiteral("此文件不支持文本预览。")); return; }
    if (m_files.size() == 8) m_files.removeFirst();
    m_files.append({relative, QString::fromUtf8(bytes), QFileInfo(canonical).suffix().toLower()});
    m_active = m_files.size() - 1;
    fail({}); emit previewChanged();
}
void WorkspaceController::selectTab(int index) { if (index < 0 || index >= m_files.size()) return; m_active = index; emit previewChanged(); }
void WorkspaceController::closeTab(int index)
{
    if (index < 0 || index >= m_files.size()) return;
    m_files.removeAt(index);
    if (m_active > index) --m_active;
    else if (m_active == index) m_active = qMin(index, int(m_files.size()) - 1);
    emit previewChanged();
}
}
