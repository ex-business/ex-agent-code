#include "application/agent/agent_controller.h"
#include "exchange.h"
#include "task_state.h"
#include <QFileInfo>
#include <QJsonObject>
#include <QUuid>

namespace exagent {
using namespace supervision;
namespace {
constexpr auto Tasks = "supervisedTasks";
bool uuid(const QJsonValue &value) { return value.isString() && !QUuid(value.toString()).isNull(); }
}

QString AgentController::workerHost() const
{
    // 独立配置保存随机实例身份，跨机器同名目录也不会被当成同一个实例。
    return m_workerHost;
}

bool AgentController::exportSupervisedTask(const QString &task, const QString &host, const QUrl &directory)
{
    auto *owner = find(m_selected);
    if (!m_writable || !owner || QUuid(host).isNull() || host == workerHost() || !directory.isLocalFile()) return false;
    const auto root = QFileInfo(directory.toLocalFile()).canonicalFilePath();
    auto tasks = supervisedTasks();
    for (qsizetype index = 0; index < tasks.size(); ++index) {
        auto row = tasks[index].toMap();
        const int state = row.value("state").toInt();
        if (row.value("id") != task || (state != Pending && state != Failed && state != Uncertain)) continue;
        if (state != Uncertain && row.contains("resultDelivery") && row.value("resultDelivery").toInt() == 0) return false;
        if (row.value("decisionRequired").toInt() == 1 && row.value("approved").toInt() != 1) {
            const auto before = owner->history;
            row.insert("state", Decision);
            tasks[index] = row;
            owner->history.insert(Tasks, tasks);
            if (!save()) { owner->history = before; return false; }
            emit supervisionChanged();
            return false;
        }
        const auto before = owner->history;
        const auto dispatch = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request{{"version", 1}, {"type", 1}, {"task", task}, {"dispatch", dispatch},
            {"ownerHost", workerHost()}, {"ownerSession", owner->id}, {"targetHost", host}, {"token", token},
            {"text", row.value("text").toString()}};
        auto attempts = row.value("attempts").toList();
        attempts.append(QVariantMap{{"dispatch", dispatch}, {"host", host}, {"state", Uncertain}});
        row.insert("dispatch", dispatch);
        row.insert("remoteHost", host);
        row.insert("exchangeRequest", request.toVariantMap());
        row.insert("attempts", attempts);
        row.insert("approved", 0);
        row.insert("state", Uncertain);
        row.remove("run");
        row.remove("result");
        row.remove("resultDelivery");
        row.remove("remoteResult");
        tasks[index] = row;
        owner->history.insert(Tasks, tasks);
        if (!save()) { owner->history = before; return false; }
        // 文件只描述任务。接收端显式导入并批准后，才使用其本机账号和权限执行。
        QString error;
        if (!writeEnvelope(root, dispatch + ".ex-worker.json", request, &error)) {
            emit failure(error);
            emit supervisionChanged();
            return false;
        }
        row.insert("state", Working);
        auto attempt = attempts.last().toMap();
        attempt.insert("state", Working);
        attempts.last() = attempt;
        row.insert("attempts", attempts);
        tasks[index] = row;
        owner->history.insert(Tasks, tasks);
        const bool stored = save();
        emit supervisionChanged();
        return stored;
    }
    return false;
}

bool AgentController::importSupervisedExchange(const QUrl &file, const QString &workerId)
{
    auto *owner = find(m_selected);
    if (!m_writable || !owner || !file.isLocalFile()) return false;
    QString error;
    const auto data = readEnvelope(file.toLocalFile(), &error);
    if (data.isEmpty()) { emit failure(error); return false; }
    if (!uuid(data["dispatch"]) || !uuid(data["task"]) || !uuid(data["token"])
        || !uuid(data["ownerHost"]) || !uuid(data["targetHost"]) || data["ownerSession"].toString().isEmpty()) return false;
    if (!data["type"].isDouble() || (data["type"].toDouble() != 1.0 && data["type"].toDouble() != 2.0)) return false;
    const int type = data["type"].toInt();
    auto tasks = supervisedTasks();
    const auto before = owner->history;
    if (type == 1) {
        const auto *worker = find(workerId);
        const auto text = data["text"].toString();
        if (data["targetHost"] != workerHost() || !worker || worker == owner || worker->projectId != owner->projectId
            || text.trimmed().isEmpty() || text.size() > 8192) return false;
        for (const auto &session : m_sessions)
            for (const auto &entry : session.history.value(Tasks).toList()) {
                const auto prior = entry.toMap().value("remoteEnvelope").toMap();
                if (prior.value("dispatch").toString() == data["dispatch"].toString()) return prior == data.toVariantMap();
            }
        tasks.append(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"worker", workerId},
            {"title", worker->title}, {"text", text}, {"state", Decision}, {"decisionRequired", 1}, {"approved", 0},
            {"attempts", QVariantList{}}, {"remoteEnvelope", data.toVariantMap()},
            {"exchangeRoot", QFileInfo(file.toLocalFile()).canonicalPath()}});
    } else if (type == 2) {
        if (!data["outcome"].isDouble() || data["outcome"].toDouble() != data["outcome"].toInt(-1)) return false;
        if (data["ownerHost"] != workerHost() || data["ownerSession"] != owner->id
            || (data["outcome"].toInt(-1) != Completed && data["outcome"].toInt(-1) != Failed)
            || !data["result"].isString() || data["result"].toString().size() > 8192) return false;
        bool matched = false;
        for (auto &entry : tasks) {
            auto row = entry.toMap();
            const auto request = row.value("exchangeRequest").toMap();
            if (row.value("id").toString() != data["task"].toString()
                || row.value("dispatch").toString() != data["dispatch"].toString()) continue;
            for (const auto *key : {"token", "targetHost", "ownerHost", "ownerSession"})
                if (request.value(key).toString() != data[key].toString()) return false;
            if (row.contains("remoteResult")) return row.value("remoteResult").toMap() == data.toVariantMap();
            if (row.value("state").toInt() != Working && row.value("state").toInt() != Uncertain) return false;
            row.insert("remoteResult", data.toVariantMap());
            row.insert("state", data["outcome"].toInt());
            row.insert("result", QStringLiteral("Worker %1\n%2").arg(data["targetHost"].toString(), data["result"].toString()).left(8192));
            row.insert("resultDelivery", 0);
            auto attempts = row.value("attempts").toList();
            if (!attempts.isEmpty()) {
                auto attempt = attempts.last().toMap();
                attempt.insert("state", row.value("state"));
                attempt.insert("result", row.value("result"));
                attempts.last() = attempt;
                row.insert("attempts", attempts);
            }
            entry = row;
            matched = true;
        }
        if (!matched) return false;
    } else return false;
    owner->history.insert(Tasks, tasks);
    if (!save()) { owner->history = before; return false; }
    m_supervisionPump.start(0);
    emit supervisionChanged();
    return true;
}
}
