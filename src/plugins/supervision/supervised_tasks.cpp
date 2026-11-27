#include "application/agent/agent_controller.h"
#include "domain/content/content_block.h"
#include <QUuid>
#include "task_state.h"
#include "exchange.h"
#include <QJsonObject>

namespace exagent {
namespace {
using namespace supervision;
constexpr auto Tasks = "supervisedTasks";
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

QVariantList AgentController::supervisedTasks() const
{
    const auto *s = find(m_selected);
    return s ? s->history.value(Tasks).toList() : QVariantList{};
}

bool AgentController::createSupervisedTask(const QString &target, const QString &text, bool decisionRequired)
{
    auto *owner = find(m_selected);
    const auto *worker = find(target);
    if (!m_writable || !owner || !worker || owner == worker || owner->projectId != worker->projectId
        || text.trimmed().isEmpty() || text.size() > 8192) return false;
    const auto before = owner->history;
    auto tasks = supervisedTasks();
    tasks.append(QVariantMap{{"id", id()}, {"worker", target}, {"title", worker->title}, {"text", text},
        {"state", decisionRequired ? Decision : Pending}, {"decisionRequired", decisionRequired ? 1 : 0}, {"approved", 0}, {"attempts", QVariantList{}}});
    owner->history.insert(Tasks, tasks);
    if (!save()) { owner->history = before; return false; }
    emit supervisionChanged();
    return true;
}

bool AgentController::resolveSupervisedTask(const QString &task, bool allow)
{
    auto *owner = find(m_selected);
    if (!m_writable || !owner) return false;
    auto tasks = supervisedTasks();
    for (auto &value : tasks) {
        auto row = value.toMap();
        if (row.value("id") != task || row.value("state").toInt() != Decision) continue;
        const auto before = owner->history;
        row.insert("state", allow ? Pending : Decision);
        row.insert("approved", allow ? 1 : 0);
        row.insert("result", allow ? QString{} : QStringLiteral("监督者拒绝了此任务。"));
        value = row;
        owner->history.insert(Tasks, tasks);
        if (!save()) { owner->history = before; return false; }
        emit supervisionChanged();
        return true;
    }
    return false;
}

bool AgentController::dispatchSupervisedTask(const QString &task)
{
    auto *owner = find(m_selected);
    if (!m_writable || !owner) return false;
    auto tasks = supervisedTasks();
    // 保存会共享Qt容器快照，跨保存点再次按下标写入以触发分离，不能沿用旧元素引用。
    for (qsizetype index = 0; index < tasks.size(); ++index) {
        auto row = tasks[index].toMap();
        const int state = row.value("state").toInt();
        if (row.value("id") != task || (state != Pending && state != Failed && state != Uncertain)) continue;
        if (row.value("decisionRequired").toInt() == 1 && row.value("approved").toInt() != 1) {
            const auto before = owner->history;
            row.insert("state", Decision);
            tasks[index] = row;
            owner->history.insert(Tasks, tasks);
            if (!save()) { owner->history = before; return false; }
            emit supervisionChanged();
            return false;
        }
        if (state != Uncertain && row.contains("resultDelivery") && row.value("resultDelivery").toInt() == 0) {
            emit failure(QStringLiteral("上次执行结果仍在等待回传，请完成核对后再重试。"));
            return false;
        }
        if (!row.value("remoteHost").toString().isEmpty()) return false;
        if (row.contains("remoteEnvelope") && !row.value("attempts").toList().isEmpty()) {
            emit failure(QStringLiteral("远端任务重试需要监督者生成新的派发文件。"));
            return false;
        }
        const auto workerId = row.value("worker").toString();
        const auto *worker = find(workerId);
        if (!worker || worker == owner || worker->projectId != owner->projectId || activeSession(workerId)) return false;
        const QString dispatch = id();
        const auto before = owner->history;
        auto attempts = row.value("attempts").toList();
        attempts.append(QVariantMap{{"dispatch", dispatch}, {"state", Uncertain}});
        row.insert("attempts", attempts);
        row.insert("dispatch", dispatch);
        row.insert("state", Uncertain);
        row.insert("approved", 0);
        row.remove("run");
        row.remove("result");
        row.remove("resultDelivery");
        tasks[index] = row;
        // 派发意图先落盘。进程在调用或回执之间退出时保留不确定状态，由用户显式核对再重试。
        owner->history.insert(Tasks, tasks);
        if (!save()) { owner->history = before; return false; }
        const QString ownerId = owner->id;
        const QString run = submitWorker(workerId, row.value("text").toString());
        owner = find(ownerId);
        if (!owner) return false;
        // submitWorker 发出的同步视图信号可能切换选择，后续写回始终绑定原监督会话。
        row.insert("run", run);
        row.insert("state", run.isEmpty() ? Failed : Working);
        auto attempt = attempts.last().toMap();
        attempt.insert("run", run);
        attempt.insert("state", row.value("state"));
        attempts.last() = attempt;
        row.insert("attempts", attempts);
        tasks[index] = row;
        owner->history.insert(Tasks, tasks);
        if (!save()) return false;
        emit supervisionChanged();
        return !run.isEmpty();
    }
    return false;
}

void AgentController::finishSupervised(const QString &workerId, const QString &run, int outcome)
{
    const auto *worker = find(workerId);
    if (!worker) return;
    for (auto &owner : m_sessions) {
        auto tasks = owner.history.value(Tasks).toList();
        for (qsizetype index = 0; index < tasks.size(); ++index) {
            auto row = tasks[index].toMap();
            if (row.value("worker") != workerId || row.value("run") != run
                || (row.value("state").toInt() != Working && row.value("state").toInt() != Uncertain) || owner.projectId != worker->projectId) continue;
            QString result;
            for (const auto &message : worker->messages)
                if (message.role == MessageRole::Assistant && message.activity.value("runId") == run)
                    result += message.content + '\n';
            if (result.isEmpty()) result = outcome == int(content::TaskState::Complete)
                ? QStringLiteral("执行完成，未返回正文。") : QStringLiteral("执行未完成，请核对 worker 会话。");
            const int state = outcome == int(content::TaskState::Complete) ? Completed
                : outcome == int(content::TaskState::Interrupted) ? Uncertain : Failed;
            row.insert("state", state);
            row.insert("result", result.left(8192));
            if (state != Uncertain) row.insert("resultDelivery", 0);
            else row.remove("resultDelivery");
            auto attempts = row.value("attempts").toList();
            if (!attempts.isEmpty()) {
                auto attempt = attempts.last().toMap();
                attempt.insert("state", state);
                attempt.insert("result", result.left(8192));
                attempts.last() = attempt;
                row.insert("attempts", attempts);
            }
            tasks[index] = row;
            owner.history.insert(Tasks, tasks);
            m_supervisionPump.start(save() ? 0 : 5000);
            emit supervisionChanged();
        }
    }
}
void AgentController::deliverSupervisedResults()
{
    // 磁盘恢复后先补落盘，外部文件和信箱只发布已有持久意图的最终结果。
    if (!save()) { m_supervisionPump.start(5000); return; }
    QStringList owners;
    for (const auto &s : m_sessions) owners.append(s.id);
    bool pending = false;
    for (const auto &ownerId : owners) {
        auto *owner = find(ownerId);
        if (!owner) continue;
        const auto snapshot = owner->history.value(Tasks).toList();
        for (const auto &value : snapshot) {
            const auto row = value.toMap();
            if (!row.contains("resultDelivery") || row.value("resultDelivery").toInt() != 0) continue;
            if (row.value("state").toInt() == Uncertain) continue;
            const auto remote = row.value("remoteEnvelope").toMap();
            if (!remote.isEmpty()) {
                auto envelope = QJsonObject::fromVariantMap(remote);
                envelope.insert("type", 2);
                envelope.insert("outcome", row.value("state").toInt());
                envelope.insert("result", row.value("result").toString());
                QString error;
                if (!writeEnvelope(row.value("exchangeRoot").toString(), remote.value("dispatch").toString() + ".result.ex-worker.json", envelope, &error)) {
                    pending = true;
                    continue;
                }
            }
            // 与结果一起保存待投递标记，重复调用使用同一派发ID，崩溃后重放只补回信。
            if (!enqueueMail(row.value("worker").toString(), ownerId, row.value("result").toString(), row.value("dispatch").toString())) {
                pending = true;
                continue;
            }
            owner = find(ownerId);
            if (!owner) continue;
            const auto before = owner->history;
            auto tasks = owner->history.value(Tasks).toList();
            for (auto &entry : tasks) {
                auto current = entry.toMap();
                if (current.value("id") != row.value("id") || current.value("dispatch") != row.value("dispatch")) continue;
                current.insert("resultDelivery", 1);
                entry = current;
            }
            owner->history.insert(Tasks, tasks);
            if (!save()) { owner->history = before; pending = true; }
        }
    }
    if (pending) m_supervisionPump.start(5000);
}

}
