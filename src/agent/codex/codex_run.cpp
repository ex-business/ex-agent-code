#include "codex_run.h"
#include "plugins/session_mail/mail.h"
#include "plugins/diff_review/codex_evidence.h"
#include "plugins/agent_activity/activity_store.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include "domain/content/content_block.h"
#include "domain/agent/account_status.h"

namespace exagent::codex {
using State = content::TaskState;
Run::Run(QJsonObject spec, QObject *parent)
    : RuntimeRun(parent)
    , m_spec(std::move(spec))
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(30000);
    connect(&m_deadline, &QTimer::timeout, this,
        [this] { finish(int(State::Interrupted), QStringLiteral("引擎应答超时，未自动重新派发。")); });
    m_flush.setSingleShot(true);
    m_flush.setInterval(32);
    connect(&m_flush, &QTimer::timeout, this, &Run::flushOutput);
    connect(&m_connection, &Connection::failure, this,
        [this](const QString &reason) { finish(int(State::Interrupted), reason); });
    connect(&m_connection, &Connection::frame, this, &Run::receive);
    connect(&m_connection, &Connection::ready, this, [this] {
        request("initialize",
            { { "clientInfo",
                QJsonObject {
                    { "name", "ex_agent_code" }, { "title", "EX Agent Code" }, { "version", "0.1" } } } });
    });
}
void Run::report(int status)
{
    if (m_spec["reconcile"].toBool()) return;
    m_activity.insert("status",status);
    m_activity.insert("session",m_spec["conversationId"].toString());
    activity::publish(m_spec["cwd"].toString(),m_spec["conversationId"].toString(),m_activity);
}
void Run::start()
{
    m_deadline.start();
    report(activity::Working);
    m_connection.start(Account(m_spec["account"].toInt()), m_spec["cwd"].toString(),m_spec["accountHome"].toString());
}
void Run::emitEvent(QJsonObject data)
{
    data.insert("conversationId", m_spec["conversationId"]);
    data.insert("runId", m_spec["runId"]);
    emit produced(data);
}
void Run::request(const QString &method, const QJsonObject &params)
{
    const int id = ++m_id;
    m_pending.insert(id, method);
    if(method!="account/rateLimits/read")m_deadline.start();
    if (!m_connection.send({ { "id", id }, { "method", method }, { "params", params } }))
        finish(int(State::Interrupted), QStringLiteral("无法向 Codex 写入请求。"));
}
void Run::finish(int state, const QString &detail)
{
    if (m_done)
        return;
    m_done = true;
    m_deadline.stop();
    flushOutput();
    if (m_spec["reconcile"].toBool() && !m_dispatched)
        state = int(State::Interrupted);
    report(state == int(State::Complete) ? activity::Done : state == int(State::Cancelled) ? activity::Stopped : activity::Failed);
    emitEvent({ { "kind", "end" }, { "state", state }, { "detail", detail } });
    emit ended();
}
void Run::interrupt()
{
    if (m_done || m_stopping)
        return;
    m_stopping = true;
    if (!m_dispatched) {
        finish(int(State::Cancelled), QStringLiteral("已取消派发。"));
        return;
    }
    if (!m_turn.isEmpty())
        request("turn/interrupt", { { "threadId", m_thread }, { "turnId", m_turn } });
}
bool Run::steer(const QString &id, const QString &turn, const QString &text)
{
    if (m_done || m_stopping || !m_dispatched || m_spec["reconcile"].toBool() || turn.isEmpty()
        || turn != m_turn || QUuid(id).isNull() || text.isEmpty() || text.size() > mail::MaxText + 256 || !m_mail.isEmpty()) return false;
    const int requestId = ++m_id;
    const QJsonObject params{{"threadId", m_thread}, {"expectedTurnId", turn}, {"clientUserMessageId", id},
        {"input", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}};
    if (!m_connection.send({{"id", requestId}, {"method", "turn/steer"}, {"params", params}})) return false;
    m_mail.insert(requestId, id);
    // 单封信超时只影响回执；保留原回合及其独立的启动和停止期限。
    QTimer::singleShot(15000, this, [this, requestId] {
        const auto pending = m_mail.take(requestId);
        if (!pending.isEmpty() && !m_done)
            emitEvent({{"kind", "mailReceipt"}, {"mailId", pending}, {"turnId", m_turn}, {"state", mail::Unconfirmed}});
    });
    return true;
}

void Run::receive(const QJsonObject &frame)
{
    if (m_done)
        return;
    const auto method = frame["method"].toString();
    const auto params = frame["params"].toObject();
    if (!method.isEmpty()) {
        if (frame.contains("id"))
            approval(method, frame["id"], params);
        else
            notification(method, params);
        return;
    }
    const auto mailId = m_mail.take(frame["id"].toInt(-1));
    if (!mailId.isEmpty()) {
        const int state = frame.contains("error") ? mail::Rejected
            : frame["result"].toObject()["turnId"] == m_turn ? mail::Accepted : mail::Unconfirmed;
        emitEvent({{"kind", "mailReceipt"}, {"mailId", mailId}, {"turnId", m_turn}, {"state", state}});
        return;
    }
    const auto pending = m_pending.take(frame["id"].toInt(-1));
    if (pending.isEmpty())
        return;
    if (std::all_of(m_pending.cbegin(),m_pending.cend(),[](const QString &method){return method=="account/rateLimits/read";}))
        m_deadline.stop();
    if (frame.contains("error")) {
        if(pending=="account/rateLimits/read")return;
        finish(int(State::Failed),
            QStringLiteral("Codex 拒绝 %1（错误码 %2）。")
                .arg(pending)
                .arg(frame["error"].toObject()["code"].toInt()));
        return;
    }
    result(pending, frame["result"].toObject());
}
void Run::result(const QString &method, const QJsonObject &value)
{
    if(method=="account/rateLimits/read") {
        const auto limits=value["rateLimits"].toObject();
        if(!limits.isEmpty())emitEvent({{"kind","accountLimits"},{"limits",limits}});
    } else if (method == "initialize") {
        m_connection.send({ { "method", "initialized" } });
        request("account/read", { { "refreshToken", false } });
    } else if (method == "account/read") {
        const auto account = value["account"].toObject();
        if ((account.isEmpty() && value["requiresOpenaiAuth"].toBool(true))
            || (m_spec["account"].toInt() == Dedicated && account["type"] != "apiKey")) {
            emitEvent({{"kind","accountUnavailable"},{"status",accounts::NeedsLogin}});
            finish(int(State::Failed), QStringLiteral("所选账号未登录，请先配置 Codex 凭据。"));
            return;
        }
        emitEvent({{"kind","accountInfo"},{"owner",account["email"]},{"plan",account["planType"]}});
        const auto owner = identity(Account(m_spec["account"].toInt()), account);
        const auto previous = m_spec["identity"].toString();
        if (!m_spec["threadId"].toString().isEmpty() && (owner.isEmpty() || owner != previous)) {
            if (m_spec["reconcile"].toBool() && !previous.isEmpty()) {
                finish(int(State::Interrupted), QStringLiteral("原回合的账号已变化，无法核对。"));
                return;
            }
            // 旧记录可以由用户显式读取；新一轮缺少可靠身份时从当前输入开始。
            if (!m_spec["reconcile"].toBool())
                m_spec.remove("threadId");
        }
        m_spec.insert("identity", owner);
        if (m_spec["reconcile"].toBool()) {
            // 对账只读取原线程。断线后没有可靠 turnId 时不能猜测或重新执行。
            m_thread = m_spec["threadId"].toString();
            m_turn = m_spec["turnId"].toString();
            request("thread/read", { { "threadId", m_thread }, { "includeTurns", true } });
            return;
        }
        request("account/rateLimits/read",{});
        // 完全访问仅对当前会话明确启用，恢复线程时也校验引擎实际采用的权限。
        QJsonObject options { { "cwd", m_spec["cwd"] }, { "approvalPolicy", m_spec["fullAccess"].toBool() ? "never" : "on-request" },
            { "approvalsReviewer", "user" },
            { "sandbox", m_spec["fullAccess"].toBool() ? "danger-full-access" : m_spec["write"].toBool() ? "workspace-write" : "read-only" } };
        if (!m_spec["model"].toString().isEmpty())
            options.insert("model", m_spec["model"]);
        const auto bound = m_spec["threadId"].toString();
        if (!bound.isEmpty())
            options.insert("threadId", bound);
        if (!bound.isEmpty() && !(m_input = claimThread(bound))) {
            finish(int(State::Failed), QStringLiteral("线程输入已被占用，请先退出交互终端。"));
            return;
        }
        request(bound.isEmpty() ? "thread/start" : "thread/resume", options);
    } else if (method == "thread/read") {
        const auto thread = value["thread"].toObject();
        if (thread["id"] == m_thread)
            for (const auto &entry : thread["turns"].toArray()) {
                const auto turn = entry.toObject();
                if (turn["id"] != m_turn)
                    continue;
                // 仅用引擎快照修复已知回合的展示；永不重发该回合的工具调用。
                m_dispatched = true;
                for (const auto &item : turn["items"].toArray())
                    notification(
                        "item/completed", { { "threadId", m_thread }, { "turnId", m_turn }, { "item", item } });
                if (turn["status"] == "inProgress")
                    finish(int(State::Interrupted), QStringLiteral("引擎回合仍在进行，可稍后再次核对。"));
                else
                    notification("turn/completed", { { "threadId", m_thread }, { "turn", turn } });
                return;
            }
        finish(int(State::Interrupted), QStringLiteral("引擎历史中尚未找到原回合。"));
    } else if (method == "thread/start" || method == "thread/resume") {
        const auto sandbox = value["sandbox"].toObject();
        m_thread = value["thread"].toObject()["id"].toString();
        const auto cwd = QFileInfo(value["cwd"].toString()).canonicalFilePath();
        bool rootsMatch = true;
        for (const auto &root : sandbox["writableRoots"].toArray()) {
            const auto path = QFileInfo(root.toString()).canonicalFilePath();
            if (path.isEmpty() || (path != cwd && !path.startsWith(cwd + '/')))
                rootsMatch = false;
        }
        if (!rootsMatch || m_thread.isEmpty()
            || (method == "thread/resume" && m_thread != m_spec["threadId"].toString())
            || cwd != m_spec["cwd"].toString() || value["approvalPolicy"] != (m_spec["fullAccess"].toBool() ? "never" : "on-request")
            || value["approvalsReviewer"] != "user"
            || sandbox["type"] != (m_spec["fullAccess"].toBool() ? "dangerFullAccess" : m_spec["write"].toBool() ? "workspaceWrite" : "readOnly")) {
            finish(int(State::Failed), QStringLiteral("引擎返回的目录、线程或权限与派发设置不符。"));
            return;
        }
        if (!m_input && !(m_input = claimThread(m_thread))) {
            finish(int(State::Failed), QStringLiteral("无法取得线程输入权。"));
            return;
        }
        emitEvent({ { "kind", "thread" }, { "identity", m_spec["identity"] }, { "threadId", m_thread },
            { "model", value["model"] } });
        m_dispatched = true;
        QJsonArray input;
        if (!m_spec["context"].toString().isEmpty())
            input.append(QJsonObject { { "type", "text" },
                { "text", QStringLiteral("以下为用户选择的历史记录：\n") + m_spec["context"].toString() } });
        const auto element = m_spec["designElement"].toObject();
        if (!element.isEmpty()) {
            const auto payload = QJsonDocument(element).toJson(QJsonDocument::Compact);
            input.append(QJsonObject{{"type", "text"}, {"text", QStringLiteral("用户点选的网页元素。以下是页面数据：\n") + QString::fromUtf8(payload)}});
        }
        for (const auto &image : m_spec["images"].toArray())
            input.append(QJsonObject { { "type", "localImage" }, { "path", image } });
        input.append(QJsonObject { { "type", "text" }, { "text", m_spec["prompt"] } });
        request("turn/start", { { "threadId", m_thread }, { "input", input } });
    } else if (method == "turn/start") {
        const auto id = value["turn"].toObject()["id"].toString();
        if (id.isEmpty() || (!m_turn.isEmpty() && id != m_turn)) {
            finish(int(State::Interrupted), QStringLiteral("无法核对本次回合标识。"));
            return;
        }
        const bool newlyBound = m_turn.isEmpty();
        m_turn = id;
        emitEvent({ { "kind", "turn" }, { "turnId", id } });
        if (m_stopping && newlyBound)
            request("turn/interrupt", { { "threadId", m_thread }, { "turnId", id } });
    } else if (method == "turn/interrupt") {
        // 停止已受理；只有引擎终态可以证明工具确实结束。
        m_deadline.start();
    }
}
void Run::approval(const QString &method, const QJsonValue &id, const QJsonObject &params)
{
    const bool scope
        = m_dispatched && !m_turn.isEmpty() && params["threadId"] == m_thread && params["turnId"] == m_turn;
    if (method != "item/commandExecution/requestApproval" && method != "item/fileChange/requestApproval") {
        m_connection.send({ { "id", id },
            { "error", QJsonObject { { "code", -32601 }, { "message", "Unsupported client request" } } } });
        return;
    }
    if (!scope || m_stopping || m_approvals.size() >= 8) {
        m_connection.send({ { "id", id }, { "result", QJsonObject { { "decision", "decline" } } } });
        return;
    }
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_approvals.insert(token, id);
    const auto action
        = method.startsWith("item/fileChange") ? QStringLiteral("修改文件") : QStringLiteral("执行命令");
    const auto text = action + '\n' + params["command"].toString() + '\n' + params["reason"].toString() + '\n'
        + params["grantRoot"].toString() + '\n' + params["cwd"].toString(m_spec["cwd"].toString());
    if (text.size() > 32000) {
        decide(token, false);
        return;
    }
    report(activity::Waiting);
    emitEvent({ { "kind", "approval" }, { "token", token }, { "detail", text } });
}
bool Run::decide(const QString &token, bool allow)
{
    auto it = m_approvals.find(token);
    // 停止后只允许拒绝，避免晚到的旧授权启动工具。
    if (m_done || (m_stopping && allow) || it == m_approvals.end())
        return false;
    const auto id = it.value();
    m_approvals.erase(it);
    if (m_approvals.isEmpty()) report(activity::Working);
    return m_connection.send(
        { { "id", id }, { "result", QJsonObject { { "decision", allow ? "accept" : "decline" } } } });
}
void Run::output(const QString &item, const QString &kind, const QString &text, bool replace, const QString &title)
{
    if (item.isEmpty())
        return;
    // 合并相邻增量，最终快照单独发布；限制单个宿主帧，避免长输出阻塞控制消息。
    if (!replace && !m_output.isEmpty() && m_output.last()["item"] == item
        && m_output.last()["text"].toString().size() + text.size() < 20000) {
        auto &last = m_output.last();
        last.insert("text", last["text"].toString() + text);
    } else {
        for (qsizetype start = 0; start < qMax(qsizetype(1), text.size()); start += 20000) {
            m_output.append({ { "kind", "output" }, { "item", item }, { "type", kind }, { "title", title },
                { "replace", replace && start == 0 }, { "text", text.mid(start, 20000) } });
            if (m_output.size() >= 16)
                flushOutput();
        }
    }
    if (!m_flush.isActive())
        m_flush.start();
}
void Run::flushOutput()
{
    m_flush.stop();
    const auto batch = std::exchange(m_output, {});
    for (const auto &item : batch)
        emitEvent(item);
}
void Run::notification(const QString &method, const QJsonObject &params)
{
    if (method == "account/rateLimits/updated" && m_dispatched) {
        const auto limits = params["rateLimits"].toObject();
        emitEvent({{"kind","accountLimits"},{"limits",limits}});
        for (const auto& entry : {QStringLiteral("primary"), QStringLiteral("secondary")}) {
            const auto window = limits[entry].toObject();
            const auto percent = window["usedPercent"].toInt(-1);
            m_activity.remove(entry);
            if (percent >= 0 && percent <= 100) {
                QVariantMap reading {{"percent",percent}};
                if (window["resetsAt"].toInteger() > 0)
                    reading.insert("reset",window["resetsAt"].toInteger());
                m_activity.insert(entry,reading);
            }
        }
        report(m_approvals.isEmpty() ? activity::Working : activity::Waiting);
        return;
    }
    if (!m_dispatched || params["threadId"] != m_thread)
        return;
    if (method == "serverRequest/resolved") {
        const auto token = m_approvals.key(params["requestId"]);
        if (!token.isEmpty()) {
            m_approvals.remove(token);
            if (m_approvals.isEmpty()) report(activity::Working);
            emitEvent({ { "kind", "approvalClosed" }, { "token", token } });
        }
        return;
    }
    const auto turn = params["turn"].toObject();
    const auto id = params["turnId"].toString(turn["id"].toString());
    // 此连接只发一轮；通知可能先于 turn/start 的响应，仍须核对线程和回合。
    if (id.isEmpty() || (!m_turn.isEmpty() && id != m_turn))
        return;
    if (m_turn.isEmpty()) {
        if (method != "turn/started")
            return;
        m_turn = id;
        emitEvent({ { "kind", "turn" }, { "turnId", id } });
        if (m_stopping)
            request("turn/interrupt", { { "threadId", m_thread }, { "turnId", id } });
    }
    if (method == "thread/tokenUsage/updated") {
        const auto last=params["tokenUsage"].toObject()["last"].toObject();
        QJsonObject usage;for(const auto &pair:QList<QPair<QString,QString>>{{"inputTokens","input_tokens"},{"outputTokens","output_tokens"},{"totalTokens","total_tokens"}})if(last.contains(pair.first))usage.insert(pair.second,last[pair.first]);
        emitEvent({{"kind","usage"},{"usage",usage}});
        const auto count = params["tokenUsage"].toObject()["last"].toObject()["totalTokens"].toInteger(-1);
        if (count >= 0) m_activity.insert("tokens",count);
        else m_activity.remove("tokens");
        report(m_approvals.isEmpty() ? activity::Working : activity::Waiting);
        return;
    }
    const auto item = params["item"].toObject();
    const auto itemId = params["itemId"].toString(item["id"].toString());
    if (method == "item/agentMessage/delta")
        output(itemId, "assistant", params["delta"].toString(), false);
    else if (method == "item/commandExecution/outputDelta")
        output(itemId, "terminal", params["delta"].toString(), false);
    else if (method == "item/started" || method == "item/completed") {
        const auto type = item["type"].toString();
        // 仅传递引擎思考阶段的起止，不读取或存储推理内容。
        if (type == "reasoning") {
            emitEvent({ { "kind", "reasoningPhase" }, { "state", int(method == "item/started" ? State::Running : State::Complete) } });
            return;
        }
        if (type == "agentMessage" && method == "item/completed")
            output(itemId, "assistant", item["text"].toString(), true);
        else if (type == "commandExecution" && (method == "item/started" || item["aggregatedOutput"].isString()))
            output(itemId, "terminal", item["aggregatedOutput"].toString(), true, item["command"].toString());
        else if (type == "fileChange") {
            QString diff;
            for (const auto &entry : item["changes"].toArray()) {
                const auto change = entry.toObject();
                diff += change["path"].toString() + '\n' + change["diff"].toString() + '\n';
            }
            output(itemId, "diff", diff, true, QStringLiteral("文件改动"));
        }
        if (method == "item/completed" && !itemId.isEmpty()
            && (type == "commandExecution" || type == "fileChange")) {
            flushOutput();
            const auto failed
                = item["status"] == "failed" || item["status"] == "declined" || item["exitCode"].toInt() != 0;
            QJsonObject end { { "kind", "itemEnd" }, { "item", itemId },
                { "state", int(failed ? State::Failed : State::Complete) }, { "exitCode", item["exitCode"] } };
            // 只把引擎确认成功的单文件读取显示为阅读记录，复合命令仍保留 Shell 摘要。
            const auto actions = item["commandActions"].toArray();
            if (!failed && type == "commandExecution" && actions.size() == 1) {
                const auto action = actions.first().toObject();
                if (action["type"] == "read" && !action["path"].toString().isEmpty())
                    end.insert("readPath", action["path"]);
            }
            if (type == "fileChange" && item["status"] == "completed") {
                const auto proofs = review::codexEvidence(m_spec["cwd"].toString(), item["changes"].toArray());
                if (!proofs.isEmpty()) end.insert("filePatches", proofs);
            }
            emitEvent(end);
        }
    } else if (method == "turn/completed") {
        const auto status = turn["status"].toString();
        if (status == "completed")
            finish(int(State::Complete), QStringLiteral("执行完成"));
        else if (status == "interrupted")
            finish(int(State::Cancelled), QStringLiteral("已停止"));
        else
            finish(status == "failed" ? int(State::Failed) : int(State::Interrupted),
                QStringLiteral("执行未完成，请检查账号、模型权限和网络。"));
    }
}
}
