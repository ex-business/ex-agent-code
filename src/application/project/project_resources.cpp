#include "project_navigator.h"

namespace exagent {
bool ProjectNavigator::available(const QString &project, const QString &workspace) const
{
    const auto *w = catalog.workspace(project, workspace);
    return workspace.isEmpty() || (w && w->state == WorkspaceState::Active && !m_changing.contains(w->path));
}

void ProjectNavigator::manageResource(const QString &providerId, const QString &projectId, const QString &workspaceId,
                                      int action, const QString &branch)
{
    const auto *w = catalog.workspace(projectId, workspaceId);
    if (!w || action < Inspect || action > Clean) return;
    if (!runningTasks || !saveCatalog) {
        emit failure(QStringLiteral("工作区资源管理依赖尚未就绪。"));
        return;
    }
    const auto path = w->path;
    auto report = [this, projectId, workspaceId, path](QVariantMap result) {
        const auto *current = catalog.workspace(projectId, workspaceId);
        if (!current) return;
        result.insert("state", int(current->state));
        if (current->state == WorkspaceState::Cleaned) {
            result.insert("managed", false);
            result.insert("cleanable", false);
            result.insert("reason", QStringLiteral("已清理目录，保留分支与聊天记录。"));
        }
        result.insert("path", path);
        if (!result.contains("branch")) result.insert("branch", current->branch);
        result.insert("tasks", runningTasks(path) + m_preparing.value(path));
        emit resourceReport(projectId, workspaceId, result);
    };
    if (m_changing.contains(path)) { report({{"reason","正在处理此工作区。"}}); return; }
    if (w->state == WorkspaceState::Cleaned) { report({{"reason","已清理目录，聊天记录仍可查看。"}}); return; }
    if (action != Inspect && (!writable() || !runningTasks(path).isEmpty() || m_preparing.contains(path)
        || (action == Archive && w->state != WorkspaceState::Active)
        || (action != Archive && w->state != WorkspaceState::Archived))) {
        report({{"reason","当前状态不允许操作，请先结束运行中的任务。"}}); return;
    }
    for (auto *provider : m_providers) {
        if (provider->id() != providerId) continue;
        if (action != Inspect) m_changing.insert(path);
        provider->resourceInfo(path, branch, action == Clean,
            [=, this](QVariantMap result) {
                if (action != Inspect) m_changing.remove(path);
                if (action != Inspect && result.value("managed").toBool()
                    && (action != Clean || result.value("cleanable").toBool())) {
                    for (auto &project : catalog.projects) for (auto &entry : project.workspaces) {
                        // 同一路径的所有绑定共用生命周期，不能从其他项目绕过归档。
                        if (entry.path != path) continue;
                        entry.state = action == Archive ? WorkspaceState::Archived
                                    : action == Clean ? WorkspaceState::Cleaned : WorkspaceState::Active;
                        entry.branch = result.value("branch").toString();
                    }
                    if (!saveCatalog()) result.insert("reason","工作区状态写入失败，请检查存储。");
                    emit catalogChanged();
                }
                report(result);
            });
        return;
    }
    report({{"reason","工作区管理插件不可用。"}});
}
}
