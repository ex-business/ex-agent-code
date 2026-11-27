#include "project_navigator.h"
#include "project_launch_config.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QUuid>

namespace exagent {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariantMap row(ProjectNavigator::RowKind kind, const QString &title, const QString &project,
                const QString &workspace = {}, const QString &session = {})
{
    return {{"kind", int(kind)}, {"title", title}, {"project", project}, {"workspace", workspace}, {"session", session}};
}
}

void ProjectNavigator::registerTaskProvider(TaskWorkspaceProvider *provider)
{
    if (!provider) return;
    for (const auto *existing : m_providers)
        if (existing->id() == provider->id()) return;
    provider->setParent(this);
    m_providers.append(provider);
}

QVariantList ProjectNavigator::taskProviders() const
{
    QVariantList actions;
    for (const auto *provider : m_providers)
        actions.append(QVariantMap{{"id", provider->id()}, {"title", provider->actionTitle()}, {"resources", provider->managesResources()}});
    return actions;
}

void ProjectNavigator::loadTaskFields(const QString &providerId, const QString &projectId, const QString &workspaceId, int request)
{
    const auto *workspace = catalog.workspace(projectId, workspaceId);
    if (workspace) for (auto *provider : m_providers) {
        if (provider->id() != providerId) continue;
        provider->loadFields(workspace->path, [this, request](QVariantList fields) { emit taskFieldsReady(request, fields); });
        return;
    }
    emit taskFieldsReady(request, {});
}

namespace {
QString configRoot(const ProjectCatalog &catalog, const QString &project, const WorkspaceBinding &workspace)
{
    // 只接受项目内已有绑定 ID，不从历史元数据读取任意文件路径。
    const auto *source = catalog.workspace(project, workspace.launch.value("source").toString());
    return source ? source->path : workspace.path;
}
}

QVariantMap ProjectNavigator::launchSettings(const QString &providerId, const QString &project, const QString &workspace) const
{
    const auto *binding = catalog.workspace(project, workspace);
    if (!binding) return {{"error","请先绑定项目工作区。"}};
    for (auto *provider : m_providers) {
        if (provider->id() != providerId) continue;
        QString error;
        const auto root = configRoot(catalog, project, *binding);
        auto map = readLaunchDefaults(root, &error);
        map.insert("hosts", provider->readyHosts(binding->path));
        map.insert("configPath", QDir(root).filePath("orca.yaml"));
        map.insert("exists", QFileInfo::exists(map["configPath"].toString()));
        map.insert("error", error);
        return map;
    }
    return {{"error","任务插件不可用。"}};
}

QString ProjectNavigator::createLaunchTemplate(const QString &project, const QString &workspace)
{
    const auto *binding = catalog.workspace(project, workspace);
    if (!writable() || !binding) return QStringLiteral("项目工作区不可写。");
    return writeLaunchTemplate(configRoot(catalog, project, *binding));
}

void ProjectNavigator::createTask(const QString &providerId, const QString &projectId,
                                  const QString &workspaceId, const QString &name, const QVariantMap &options)
{
    const auto *workspace = catalog.workspace(projectId, workspaceId);
    if (!writable() || !workspace || !available(projectId, workspaceId) || name.trimmed().isEmpty() || name.size() > 80) {
        emit failure(QStringLiteral("请检查工作区、写入权限及任务名称。"));
        return;
    }
    for (auto *provider : m_providers) {
        if (provider->id() != providerId) continue;
        const auto settings = launchSettings(providerId, projectId, workspaceId);
        const auto host = options.value("host",settings.value("host")).toString();
        const auto task = options.value("task",settings.value("task")).toString().trimmed();
        if (!settings.value("error").toString().isEmpty()) { emit failure(settings.value("error").toString()); return; }
        if (!settings.value("hosts").toMap().contains(host) || task.size() > 2048 || task.contains(QChar::Null)) {
            emit failure(QStringLiteral("运行主机尚未就绪或任务关联无效。")); return;
        }
        auto resolved = options;
        resolved.insert("host",host);
        resolved.insert("task",task);
        if (!resolved.contains("start")) resolved.insert("start",settings.value("start"));
        const QVariantMap launch{{"host",host},{"task",task},{"provider",providerId},{"name",name.trimmed()},
            {"source",workspace->launch.value("source",workspaceId)}};
        const auto sourcePath = workspace->path;
        const auto preparingTask = name.trimmed() + " · " + newId();
        m_preparing[sourcePath].append(preparingTask);
        provider->create(sourcePath, name.trimmed(), resolved, [this, projectId, sourcePath, preparingTask, launch](QString path, QString error) {
            m_preparing[sourcePath].removeOne(preparingTask);
            if (m_preparing[sourcePath].isEmpty()) m_preparing.remove(sourcePath);
            // 异步完成后仍绑定发起任务的项目，避免用户切换会话时串到其他项目。
            if (!error.isEmpty()) emit failure(error);
            else bindCreatedWorkspace(projectId, QUrl::fromLocalFile(path), launch);
        });
        return;
    }
    emit failure(QStringLiteral("任务工作区插件未注册。"));
}

void ProjectNavigator::createProject(const QString &name)
{
    const auto title = name.trimmed();
    if (!writable() || title.isEmpty() || title.size() > 120) {
        emit failure(QStringLiteral("无法创建项目，请检查写入权限和项目名称（1 至 120 个字符）。"));
        return;
    }
    const auto id = newId();
    catalog.projects.append({id, title, {}});
    emit openGroup(id, {});
}

void ProjectNavigator::bindWorkspace(const QString &projectId, const QUrl &folder)
{
    bindCreatedWorkspace(projectId, folder, {});
}

void ProjectNavigator::bindCreatedWorkspace(const QString &projectId, const QUrl &folder, const QVariantMap &launch)
{
    const QFileInfo info(folder.toLocalFile());
    if (!writable() || !folder.isLocalFile() || !info.isDir() || !info.isReadable()) {
        emit failure(QStringLiteral("请选择可读取的本地工作区文件夹。"));
        return;
    }
    const auto path = info.canonicalFilePath();
    for (auto &project : catalog.projects) {
        if (project.id != projectId) continue;
        for (const auto &existing : project.workspaces) {
            if (existing.path == path) { emit openGroup(projectId, existing.id); return; }
        }
        const auto id = newId();
        WorkspaceBinding binding{id, path};
        binding.launch = launch;
        // 同一路径可归属不同项目，新绑定继承已有的归档状态。
        for (const auto &group : catalog.projects) for (const auto &other : group.workspaces)
            if (other.path == path) { binding.state = other.state; binding.branch = other.branch; }
        project.workspaces.append(binding);
        emit openGroup(projectId, id);
        return;
    }
    emit failure(QStringLiteral("工作区所属项目不存在。"));
}

void ProjectNavigator::refresh(const QList<ChatSession> &sessions)
{
    QHash<QString, QVariantList> groups;
    for (const auto &session : sessions) {
        // 两类分组使用不同前缀，避免项目和工作区标识意外重合。
        const auto key = session.workspaceId.isEmpty() ? "p/" + session.projectId : "w/" + session.workspaceId;
        groups[key].append(row(SessionRow, session.title, session.projectId, session.workspaceId, session.id));
    }
    QVariantList next;
    for (const auto &project : catalog.projects) {
        next.append(row(ProjectRow, project.name, project.id));
        next.append(groups.value("p/" + project.id));
        for (const auto &workspace : project.workspaces) {
            auto title = QFileInfo(workspace.path).fileName();
            if (workspace.state == WorkspaceState::Archived) title += QStringLiteral("（已归档）");
            if (workspace.state == WorkspaceState::Cleaned) title += QStringLiteral("（已清理）");
            auto entry = row(WorkspaceRow, title.isEmpty() ? workspace.path : title, project.id, workspace.id);
            entry.insert("state", int(workspace.state));
            entry.insert("path", workspace.path);
            entry.insert("taskLinks", workspace.taskLinks);
            entry.insert("projectTitle", project.name);
            entry.insert("launch", workspace.launch);
            next.append(entry);
            next.append(groups.value("w/" + workspace.id));
        }
    }
    if (m_rows == next) return;
    m_rows = std::move(next);
    emit rowsChanged();
}
}
