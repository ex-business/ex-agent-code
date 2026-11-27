#include "mail.h"
#include "application/agent/agent_controller.h"
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace exagent {
namespace {
constexpr auto Inbox = "agentMail";
QVariantList entries(const ChatSession &session) { return session.history.value(Inbox).toList(); }
}

QVariantList AgentController::correspondents() const
{
    QVariantList rows;
    const auto *source = find(m_selected);
    if (!source || !m_writable) return rows;
    for (const auto &s : m_sessions)
        if (s.projectId == source->projectId && s.id != source->id)
            rows.append(QVariantMap{{"id", s.id}, {"title", s.title}});
    return rows;
}

QVariantList AgentController::correspondence() const
{
    QVariantList rows;
    const auto *source = find(m_selected);
    if (!source) return rows;
    for (const auto &s : m_sessions) {
        if (s.projectId != source->projectId) continue;
        for (const auto &value : entries(s)) {
            auto row = value.toMap();
            if (row.value("project") != s.projectId || row.value("to") != s.id
                || (s.id != source->id && row.value("from") != source->id)) continue;
            row.insert("recipient", s.title);
            rows.append(row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("time").toString() > b.toMap().value("time").toString();
    });
    return rows;
}

bool AgentController::sendMail(const QString &target, const QString &text)
{
    return m_writable && enqueueMail(m_selected, target, text, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool AgentController::enqueueMail(const QString &sourceId, const QString &target, const QString &text, const QString &id)
{
    auto *receiver = find(target);
    const auto *sender = find(sourceId);
    if (!receiver || !sender || receiver == sender || receiver->projectId != sender->projectId
        || text.trimmed().isEmpty() || text.size() > mail::MaxText || QUuid(id).isNull()) return false;
    const auto before = entries(*receiver);
    for (const auto &v : before) {
        const auto item = v.toMap();
        if (item.value("id") == id)
            return item.value("from") == sourceId && item.value("to") == target && item.value("text") == text;
    }
    auto records = before;
    records.append(QVariantMap{{"id", id}, {"project", sender->projectId}, {"from", sourceId}, {"sender", sender->title},
        {"to", target}, {"text", text}, {"state", mail::Queued}, {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    receiver->history.insert(Inbox, records);
    if (!save()) { receiver->history.insert(Inbox, before); return false; }
    emit mailChanged();
    deliverMail(target);
    return true;
}

void AgentController::deliverMail(const QString &session)
{
    if (m_worker.state() != ipc::ServiceState::Ready) return;
    const auto run = m_active.find(session);
    auto *s = find(session);
    if (!s || run == m_active.end() || run->turn.isEmpty() || run->stopping || run->reconciliation || !run->mail.isEmpty()) return;
    const auto before = entries(*s);
    auto records = before;
    for (auto &v : records) {
        auto row = v.toMap();
        if (row.value("state").toInt() != mail::Queued || row.value("to") != session || row.value("project") != s->projectId) continue;
        const auto *sender = find(row.value("from").toString());
        if (!sender || sender->projectId != s->projectId) continue;
        QString body = QStringLiteral("来自会话「%1」的消息\n%2").arg(row.value("sender").toString().left(128), row.value("text").toString());
        QString policyError;
        // 暂停的扩展会话保留信箱消息，恢复后按原有顺序投递，不绕过职责和输入约束。
        if (!applyInputPolicies(*s, MailInput, body, &policyError)) return;
        const QString id = row.value("id").toString();
        row.insert("state", mail::Sending);
        row.insert("run", run->id);
        row.insert("turn", run->turn);
        v = row;
        // 先持久化在途标记，再进入外部进程。每个回合同一时刻最多有一封未确认消息。
        s->history.insert(Inbox, records);
        if (!save()) { s->history.insert(Inbox, before); return; }
        run->mail = id;
        const auto request = m_worker.request("agent.steer", {{"conversationId", session}, {"runId", run->id},
            {"turnId", run->turn}, {"mailId", id}, {"text", body}});
        if (request.isEmpty()) settleMail(session, run->id, id, mail::Unconfirmed);
        else { m_mailRequests.insert(request, id); emit mailChanged(); }
        return;
    }
}

void AgentController::settleMail(const QString &session, const QString &runId, const QString &id, int state)
{
    auto *s = find(session);
    if (!s || id.isEmpty() || (state != mail::Accepted && state != mail::Rejected && state != mail::Unconfirmed)) return;
    const auto before = entries(*s);
    auto records = before;
    for (auto &v : records) {
        auto row = v.toMap();
        if (row.value("id") != id || row.value("run") != runId || row.value("state").toInt() != mail::Sending) continue;
        row.insert("state", state);
        v = row;
        s->history.insert(Inbox, records);
        if (!save()) {
            s->history.insert(Inbox, before);
            // 只重试已确认回执的本地落盘，不重发外部消息。
            QTimer::singleShot(1000, this, [this, session, runId, id, state] { settleMail(session, runId, id, state); });
            return;
        }
        auto active = m_active.find(session);
        if (active != m_active.end() && active->id == runId && active->mail == id) active->mail.clear();
        emit mailChanged();
        QTimer::singleShot(0, this, [this, session]{ deliverMail(session); });
        return;
    }
}

void AgentController::recoverMail(ChatSession &s)
{
    auto records = entries(s);
    bool changed = false;
    for (auto &v : records) {
        auto row = v.toMap();
        if (row.value("state").toInt() != mail::Sending || row.value("to") != s.id) continue;
        row.insert("state", mail::Unconfirmed);
        v = row;
        changed = true;
    }
    if (changed) s.history.insert(Inbox, records);
}
}
