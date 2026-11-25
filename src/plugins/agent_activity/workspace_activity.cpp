#include "application/agent/agent_controller.h"
#include "domain/content/content_block.h"
#include <algorithm>

namespace exagent {
void AgentController::indexWorkspaceActivity()
{
    // 会话载入时建立最近一轮执行的位置，打开面板和刷新徽标时无需扫描聊天正文。
    m_latestMarker.clear();
    for (const auto &s : std::as_const(m_sessions)) {
        for (qsizetype i = s.messages.size(); i > 0; --i) {
            const auto &msg = s.messages.at(i - 1);
            if (msg.role == MessageRole::Notice && msg.activity.value("serviceId") == "codex-agent") {
                m_latestMarker.insert(s.id, i - 1);
                break;
            }
        }
    }
}

int AgentController::pendingApprovalCount() const
{
    int count = 0;
    for (const auto &run : m_active)
        count += run.approvals.size();
    return count;
}

bool AgentController::requestPending(const QString &session, const QString &run, const QString &token) const
{
    const auto it = m_active.constFind(session);
    // 每个会话按到达顺序处理。历史消息和已经撤回的请求不能成为授权依据。
    return it != m_active.cend() && it->id == run && !token.isEmpty() && !it->approvals.isEmpty()
        && it->approvals.first().toMap().value("token") == token;
}

QVariantList AgentController::workspaceActivity(bool waitingOnly) const
{
    QVariantList rows;
    using State = content::TaskState;
    for (const auto &session : std::as_const(m_sessions)) {
        const auto run = m_active.constFind(session.id);
        const bool live = run != m_active.cend();
        const int pending = live ? run->approvals.size() : 0;
        if (waitingOnly && !pending) continue;

        // 每个会话只展示最近的原生执行。转发和分叉产生的副本不能接管源会话审批。
        const auto index = m_latestMarker.value(session.id, -1);
        if (index < 0 || index >= session.messages.size()) continue;
        const auto &marker = session.messages.at(index);
        if (marker.role != MessageRole::Notice || marker.activity.value("serviceId") != "codex-agent"
            || (live && marker.id != run->id)) continue;
        const auto *project = m_catalog.find(session.projectId);
        const auto time = marker.activity.value("finishedAt").toDateTime();
        int state = marker.activity.value("taskState", int(State::Interrupted)).toInt();
        // 只读实例没有原进程的输入所有权，不能用磁盘上的旧状态宣称任务正在运行。
        if (live) state = int(State::Running);
        else if (state == int(State::Created) || state == int(State::Running)) state = int(State::Interrupted);
        rows.append(QVariantMap{{"session", session.id}, {"title", session.title}, {"run", marker.id},
            {"workspace", marker.activity.value("cwd")}, {"project", project ? project->name : QString{}},
            {"time", (time.isValid() ? time : marker.createdAt).toMSecsSinceEpoch()}, {"state", state},
            {"pending", pending}, {"token", pending ? run->approvals.first().toMap().value("token") : QVariant{}},
            {"rank", pending ? 0 : live ? 1 : 2}});
    }
    std::sort(rows.begin(), rows.end(), [](const QVariant &lhs, const QVariant &rhs) {
        const auto a = lhs.toMap(), b = rhs.toMap();
        if (a["rank"] != b["rank"]) return a["rank"].toInt() < b["rank"].toInt();
        if (a["time"] != b["time"]) return a["time"].toLongLong() > b["time"].toLongLong();
        return a["session"].toString() < b["session"].toString();
    });
    return rows;
}
} // namespace exagent
