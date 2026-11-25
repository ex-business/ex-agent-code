#include "application/chat/chat_controller.h"
#include "task_provider.h"

namespace exagent {
bool ChatController::linkWorkspaceTask(const QString &projectId, const QString &workspaceId, const QString &input)
{
    const auto link = tasklinks::parse(input);
    if (!m_storageWritable || link.kind == tasklinks::Invalid) return false;
    for (auto &project : m_projects.catalog.projects) {
        if (project.id != projectId) continue;
        for (auto &workspace : project.workspaces) {
            if (workspace.id != workspaceId) continue;
            if (workspace.taskLinks.contains(link.url)) return true;
            if (workspace.taskLinks.size() >= 128) { setError(QStringLiteral("每个工作区最多关联 128 条任务。")); return false; }
            // 保存成功后才更新导航。失败时保留原索引和当前会话。
            workspace.taskLinks.append(link.url);
            QString error;
            if (!m_repository.save(m_projects.catalog,m_sessions,&error)) {
                workspace.taskLinks.removeLast();
                setError(error);
                return false;
            }
            refreshSessions();
            return true;
        }
    }
    return false;
}
}
