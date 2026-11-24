#include "project_navigator.h"
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
        actions.append(QVariantMap{{"id", provider->id()}, {"title", provider->actionTitle()}});
    return actions;
}

void ProjectNavigator::createTask(const QString &providerId, const QString &projectId,
                                  const QString &workspaceId, const QString &name)
{
    const auto *workspace = catalog.workspace(projectId, workspaceId);
    if (!writable() || !workspace || name.trimmed().isEmpty() || name.size() > 80) {
        emit failure(QStringLiteral("请检查工作区、写入权限及任务名称。"));
        return;
    }
    for (auto *provider : m_providers) {
        if (provider->id() != providerId) continue;
        provider->create(workspace->path, name.trimmed(), [this, projectId](QString path, QString error) {
            // 异步完成后仍绑定发起任务的项目，避免用户切换会话时串到其他项目。
            if (!error.isEmpty()) emit failure(error);
            else bindWorkspace(projectId, QUrl::fromLocalFile(path));
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
        project.workspaces.append({id, path});
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
            next.append(row(WorkspaceRow, title.isEmpty() ? workspace.path : title, project.id, workspace.id));
            next.append(groups.value("w/" + workspace.id));
        }
    }
    if (m_rows == next) return;
    m_rows = std::move(next);
    emit rowsChanged();
}
}
