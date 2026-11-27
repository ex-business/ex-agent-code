#include "control_host.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <tuple>
#include <QJsonArray>

namespace exagent {
QVariantMap AgentController::executionInfo(const QString &session, const QString &run) const
{
    const auto *target = find(session);
    if (!target) return {};
    for (auto it = target->messages.crbegin(); it != target->messages.crend(); ++it) {
        const auto &message = *it;
        if ((run.isEmpty() || message.id == run) && message.role == MessageRole::Notice && message.activity.value("serviceId") == "codex-agent")
            return {{"id", message.id}, {"session", session}, {"state", message.activity.value("taskState")},
                {"detail", message.content}, {"createdAt", message.timestamp()}, {"finishedAt", message.activity.value("finishedAt")}};
    }
    return {};
}
bool ProjectNavigator::setCheckpoint(const QString &projectId, const QString &workspaceId,
    const QString &comment, int state, int revision)
{
    if (!writable() || !saveCatalog || comment.size() > 4096 || comment.contains(QChar::Null) || state < int(CheckpointState::Todo) || state > int(CheckpointState::Completed)) return false;
    for (auto &project : catalog.projects) {
        if (project.id != projectId) continue;
        for (auto &workspace : project.workspaces) {
            if (workspace.id != workspaceId || workspace.checkpoint.value("revision").toInt() != revision) continue;
            const auto before = workspace.checkpoint;
            // 必须携带读取时的版本，防止 CLI 覆盖桌面或另一个 Agent 刚更新的进度。
            workspace.checkpoint = {{"comment", comment}, {"state", state}, {"revision", revision + 1},
                {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
            if (!saveCatalog()) { workspace.checkpoint = before; return false; }
            emit catalogChanged();
            return true;
        }
    }
    return false;
}
}

namespace exagent::control {
namespace {
bool applyAndSave(ChatController &chat, const std::function<void()> &action, QString *error)
{
    const auto connection = QObject::connect(chat.projects(), &ProjectNavigator::failure, &chat,
        [error](const QString &reason) { *error = reason; }, Qt::DirectConnection);
    action();
    QObject::disconnect(connection);
    // CLI成功应表示对象已提交存储；保存失败返回待核对，调用方不能盲目重建对象。
    if (!error->isEmpty()) return false;
    if (chat.saveNow(error)) return true;
    *error = QStringLiteral("操作的持久化结果待核对，请先读取对象列表再决定是否重试。原因：") + *error;
    return false;
}
QJsonObject row(ChatController &mutableChat, const QString &session)
{
    for (const auto &value : mutableChat.projects()->rows()) {
        const auto item = value.toMap();
        if (item.value("kind").toInt() != ProjectNavigator::SessionRow || item.value("session") != session) continue;
        auto result = QJsonObject::fromVariantMap(item);
        result.insert("id", session);
        result.insert("state", mutableChat.agent()->activeSession(session) ? 1 : 0);
        result.insert("lastRun", QJsonObject::fromVariantMap(mutableChat.agent()->executionInfo(session, {})));
        return result;
    }
    return {};
}
}
QJsonObject paginate(const QJsonArray &items, const QJsonObject &params, QString *error)
{
    const auto limit = params.value("limit").toInt(50);
    if (!ipc::integer(params.value("limit").isUndefined() ? QJsonValue(50) : params["limit"], 1, 100)) {
        *error = QStringLiteral("limit 必须为 1 至 100 的整数。"); return {};
    }
    qsizetype start = 0;
    const auto after = params["after"].toString();
    if (!after.isEmpty()) {
        while (start < items.size() && items[start].toObject()["id"] != after) ++start;
        if (start == items.size()) { *error = QStringLiteral("分页对象已不存在，请重新读取。"); return {}; }
        ++start;
    }
    QJsonArray output;
    for (qsizetype i = start; i < items.size() && output.size() < limit; ++i) output.append(items[i]);
    return {{"items", output}, {"next", start + output.size() < items.size() ? output.last().toObject()["id"] : QJsonValue()}};
}
void registerCore(Host &host)
{
    auto &chat = host.chat;
    host.add("agents.configure-api", {"model", "credentialFile", "write"}, [&chat](const QJsonObject &p, QString *error) {
        if(!p["model"].isString() || !p["credentialFile"].isString() || !p["write"].isBool()) {
            *error=QStringLiteral("API 配置参数无效。"); return QJsonObject{};
        }
        const auto connection=QObject::connect(chat.agent(),&AgentController::failure,&chat,[error](const QString &reason){*error=reason;},Qt::DirectConnection);
        const auto ok=chat.agent()->configureApi(p["model"].toString(),p["credentialFile"].toString(),p["write"].toBool());
        QObject::disconnect(connection);
        if(!ok && error->isEmpty())*error=QStringLiteral("当前会话不能修改 API 配置。");
        return QJsonObject{{"configured",ok}};
    });
    for (const auto &[method, kind, key] : QList<std::tuple<QString, int, QString>>{
             {"projects.list", ProjectNavigator::ProjectRow, "project"},
             {"workspaces.list", ProjectNavigator::WorkspaceRow, "workspace"},
             {"sessions.list", ProjectNavigator::SessionRow, "session"}}) {
        host.add(method, {"limit", "after"}, [&chat, kind, key](const QJsonObject &p, QString *error) {
            QJsonArray items;
            for (const auto &value : chat.projects()->rows()) {
                auto item = value.toMap();
                if (item.value("kind").toInt() != kind) continue;
                item.insert("id", item.value(key));
                items.append(QJsonObject::fromVariantMap(item));
            }
            return paginate(items, p, error);
        });
    }
    host.add("projects.create", {"name"}, [&chat](const QJsonObject &p, QString *error) {
        const auto name = p["name"].toString().trimmed();
        if (name.isEmpty() || name.size() > 120) { *error = QStringLiteral("项目名称必须为 1 至 120 个字符。"); return QJsonObject{}; }
        if (!applyAndSave(chat, [&] { chat.projects()->createProject(name); }, error)) return QJsonObject{};
        return QJsonObject{{"id", chat.currentProjectId()}, {"session", chat.currentSessionId()}};
    });
    host.add("workspaces.bind", {"project", "path"}, [&chat](const QJsonObject &p, QString *error) {
        if (!chat.projects()->catalog.find(p["project"].toString()) || !QDir::isAbsolutePath(p["path"].toString())
            || !QFileInfo(p["path"].toString()).isDir()) {
            *error = QStringLiteral("项目或工作区路径无效。"); return QJsonObject{};
        }
        if (!applyAndSave(chat, [&] { chat.projects()->bindWorkspace(p["project"].toString(), QUrl::fromLocalFile(p["path"].toString())); }, error)) return QJsonObject{};
        return QJsonObject{{"id", chat.currentWorkspaceId()}, {"session", chat.currentSessionId()}};
    });
    host.add("sessions.create", {"project", "workspace"}, [&chat](const QJsonObject &p, QString *error) {
        const auto project = p["project"].toString(), workspace = p["workspace"].toString();
        if (!chat.projects()->catalog.find(project) || (!workspace.isEmpty() && !chat.projects()->catalog.workspace(project, workspace))) {
            *error = QStringLiteral("项目或工作区不存在。"); return QJsonObject{};
        }
        if (!applyAndSave(chat, [&] { chat.newGroupSession(project, workspace); }, error)) return QJsonObject{};
        return row(chat, chat.currentSessionId());
    });
    host.add("sessions.show", {"session"}, [&chat](const QJsonObject &p, QString *error) {
        const auto result = row(chat, p["session"].toString());
        if (result.isEmpty()) *error = QStringLiteral("会话不存在。");
        return result;
    });
    host.add("sessions.select", {"session"}, [&chat](const QJsonObject &p, QString *error) {
        const auto id = p["session"].toString();
        if (row(chat, id).isEmpty()) { *error = QStringLiteral("会话不存在。"); return QJsonObject{}; }
        if (!applyAndSave(chat, [&] { chat.selectSession(id); }, error)) return QJsonObject{};
        return row(chat, id);
    });
    host.add("runs.show", {"session", "run"}, [&chat](const QJsonObject &p, QString *error) {
        if (p["run"].toString().isEmpty()) { *error = QStringLiteral("请指定运行 ID。"); return QJsonObject{}; }
        const auto info = chat.agent()->executionInfo(p["session"].toString(), p["run"].toString());
        if (info.isEmpty()) *error = QStringLiteral("运行记录不存在。");
        return QJsonObject::fromVariantMap(info);
    });
    host.add("sessions.send", {"session", "prompt"}, [&chat](const QJsonObject &p, QString *error) {
        const auto prompt = p["prompt"].toString();
        if (prompt.trimmed().isEmpty() || prompt.size() > 65536) { *error = QStringLiteral("消息长度无效。"); return QJsonObject{}; }
        const auto run = chat.agent()->submitIndependent(p["session"].toString(), prompt);
        if (run.isEmpty()) { *error = chat.errorMessage().isEmpty() ? QStringLiteral("目标会话不能派发，请检查账号、权限和活动任务。") : chat.errorMessage(); return QJsonObject{}; }
        return QJsonObject{{"run", run}, {"session", p["session"]}, {"state", 0}};
    });
    host.add("workspaces.checkpoint", {"project", "workspace", "comment", "state", "revision"}, [&chat](const QJsonObject &p, QString *error) {
        if (!p["comment"].isString() || !ipc::integer(p["state"], 0, 3) || !ipc::integer(p["revision"], 0, 2147483646)
            || !chat.projects()->setCheckpoint(p["project"].toString(), p["workspace"].toString(), p["comment"].toString(), p["state"].toInt(), p["revision"].toInt())) {
            *error = QStringLiteral("进度保存失败、版本已变化或参数无效，请先重新读取工作区。"); return QJsonObject{};
        }
        const auto *workspace = chat.projects()->catalog.workspace(p["project"].toString(), p["workspace"].toString());
        return QJsonObject::fromVariantMap(workspace->checkpoint);
    });
}
}
