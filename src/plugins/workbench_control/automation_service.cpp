#include "automation_service.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(automationLog, "exagent.automations")

namespace exagent::control {
namespace {
enum Enabled { Disabled = 0, EnabledJob = 1 };
enum RunState { Created = 0, Running = 1, Completed = 2, Failed = 3, Cancelled = 4, Interrupted = 5, Skipped = 6 };
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
}
AutomationService::AutomationService(Host &host) : QObject(&host), m_host(host)
{
    m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/automations.json";
    QFile file(m_file);
    if (QFileInfo(m_file).isSymLink()) m_valid = false;
    else if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) m_valid = false;
        else {
            const auto bytes = file.read(4 * 1024 * 1024 + 1);
            const auto data = QJsonDocument::fromJson(bytes).object();
            m_valid = bytes.size() <= 4 * 1024 * 1024 && file.error() == QFileDevice::NoError
                && data["version"].toDouble(-1) == 1 && data["jobs"].isArray();
            if (m_valid) m_jobs = data["jobs"].toArray();
        }
    }
    QSet<QString> ids;
    for (const auto &value : m_jobs) {
        const auto job = value.toObject();
        const auto id = job["id"].toString();
        if (QUuid(id).isNull() || ids.contains(id) || job["session"].toString().isEmpty()
            || !job["name"].isString() || job["name"].toString().trimmed().isEmpty() || job["name"].toString().size() > 120
            || !job["prompt"].isString() || job["prompt"].toString().trimmed().isEmpty() || job["prompt"].toString().size() > 65536
            || !ipc::integer(job["enabled"], Disabled, EnabledJob) || !ipc::integer(job["intervalSeconds"], 60, 31536000)
            || !job["runs"].isArray()) m_valid = false;
        ids.insert(id);
    }
    // 重启保留运行记录，但不重发旧输入；重新开启调度从下一周期开始。
    for (qsizetype i = 0; i < m_jobs.size(); ++i) {
        auto job = m_jobs[i].toObject();
        auto runs = job["runs"].toArray();
        for (qsizetype j = 0; j < runs.size(); ++j) {
            auto run = runs[j].toObject();
            if (run["state"].toInt() == Created || run["state"].toInt() == Running) { run.insert("state", Interrupted); runs[j] = run; }
        }
        job.insert("runs", runs);
        if (job["enabled"].toInt() == EnabledJob)
            job.insert("next", QDateTime::currentDateTimeUtc().addSecs(job["intervalSeconds"].toInt()).toString(Qt::ISODateWithMs));
        m_jobs[i] = job;
    }
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { QString error; if (!save(&error)) m_retry.start(5000); });
    connect(host.chat.agent(), &AgentController::runEnded, this, [this](const QString &s, const QString &r, int state, const QString &) { ended(s, r, state); });
    host.add("automations.list", {"limit", "after"}, [this](const QJsonObject &p, QString *error) {
        if (!m_valid) { *error = QStringLiteral("自动化存储不可读取，已暂停修改。"); return QJsonObject{}; }
        QJsonArray list;
        for (const auto &v : m_jobs) { auto job = v.toObject(); job.remove("runs"); job.insert("promptPreview", job.take("prompt").toString().left(256)); list.append(job); }
        return paginate(list, p, error);
    });
    host.add("automations.create", {"name", "session", "prompt", "intervalSeconds"}, [this](const QJsonObject &p, QString *error) {
        if (p["name"].toString().trimmed().isEmpty() || p["name"].toString().size() > 120
            || p["prompt"].toString().trimmed().isEmpty() || p["prompt"].toString().size() > 65536
            || !ipc::integer(p["intervalSeconds"], 60, 31536000)) {
            *error = QStringLiteral("自动化名称、提示词或周期无效，周期须为 60 至 31536000 秒的整数。"); return QJsonObject{};
        }
        bool exists = false;
        for (const auto &v : m_host.chat.projects()->rows()) {
            const auto row = v.toMap();
            if (row.value("kind").toInt() == ProjectNavigator::SessionRow && row.value("session").toString() == p["session"].toString()) exists = true;
        }
        if (!exists) { *error = QStringLiteral("目标会话不存在。"); return QJsonObject{}; }
        auto job = p;
        job.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
        job.insert("enabled", Disabled);
        job.insert("runs", QJsonArray{});
        const auto before = m_jobs;
        m_jobs.append(job);
        if (!save(error)) { m_jobs = before; return QJsonObject{}; }
        job.remove("runs");
        return job;
    });
    host.add("automations.set", {"id", "enabled"}, [this](const QJsonObject &p, QString *error) {
        if (!ipc::integer(p["enabled"], Disabled, EnabledJob)) { *error = QStringLiteral("enabled 必须为 0 或 1。"); return QJsonObject{}; }
        for (qsizetype i = 0; i < m_jobs.size(); ++i) {
            auto job = m_jobs[i].toObject();
            if (job["id"] != p["id"]) continue;
            const auto before = m_jobs;
            job.insert("enabled", p["enabled"]);
            job.insert("next", QDateTime::currentDateTimeUtc().addSecs(job["intervalSeconds"].toInt()).toString(Qt::ISODateWithMs));
            m_jobs[i] = job;
            if (!save(error)) { m_jobs = before; return QJsonObject{}; }
            job.remove("runs");
            return job;
        }
        *error = QStringLiteral("自动化任务不存在。"); return QJsonObject{};
    });
    host.add("automations.run", {"id"}, [this](const QJsonObject &p, QString *e) { return trigger(p["id"].toString(), false, e); });
    host.add("automations.get", {"id"}, [this](const QJsonObject &p, QString *error) {
        for (const auto &v : m_jobs) if (v.toObject()["id"] == p["id"]) { auto job = v.toObject(); job.remove("runs"); return job; }
        *error = QStringLiteral("自动化任务不存在。"); return QJsonObject{};
    });
    host.add("automations.runs", {"id", "limit", "after"}, [this](const QJsonObject &p, QString *error) {
        for (const auto &v : m_jobs) if (v.toObject()["id"] == p["id"]) return paginate(v.toObject()["runs"].toArray(), p, error);
        *error = QStringLiteral("自动化任务不存在。"); return QJsonObject{};
    });
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, &AutomationService::tick);
    if (m_valid) { QString error; if (!save(&error)) m_valid = false; else m_timer.start(); }
}
bool AutomationService::save(QString *error)
{
    if (!m_valid || QFileInfo(m_file).isSymLink()) { *error = QStringLiteral("自动化存储不可写。"); return false; }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"jobs", m_jobs}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024) { *error = QStringLiteral("自动化历史超过 4 MiB，请先人工归档。"); return false; }
    QSaveFile file(m_file);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { *error = file.errorString(); return false; }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) { file.cancelWriting(); *error = QStringLiteral("无法设置自动化存储的私有权限。"); return false; }
    if (file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    return true;
}
QJsonObject AutomationService::trigger(const QString &id, bool scheduled, QString *error)
{
    for (qsizetype i = 0; i < m_jobs.size(); ++i) {
        auto job = m_jobs[i].toObject();
        if (job["id"].toString() != id) continue;
        if (scheduled && job["enabled"].toInt() != EnabledJob) return {};
        const auto session = job["session"].toString();
        const bool busy = m_host.chat.agent()->activeSession(session);
        if (busy && !scheduled) { *error = QStringLiteral("目标会话正在执行。"); return {}; }
        const auto before = m_jobs;
        QJsonObject record{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"state", busy ? Skipped : Created}, {"createdAt", now()}};
        auto runs = job["runs"].toArray(); runs.append(record);
        job.insert("runs", runs);
        if (scheduled) job.insert("next", QDateTime::currentDateTimeUtc().addSecs(job["intervalSeconds"].toInt()).toString(Qt::ISODateWithMs));
        m_jobs[i] = job;
        // 先提交这次尝试及下一周期，CLI超时或宿主退出均不自动重放同一次输入。
        if (!save(error)) { m_jobs = before; return {}; }
        if (busy) return record;
        QString reason;
        const auto connection = connect(m_host.chat.agent(), &AgentController::failure, this, [&reason](const QString &message) { reason = message; }, Qt::DirectConnection);
        const auto run = m_host.chat.agent()->submitIndependent(session, job["prompt"].toString());
        disconnect(connection);
        record.insert("run", run);
        record.insert("state", run.isEmpty() ? Failed : Running);
        if (run.isEmpty()) {
            record.insert("reason", reason.isEmpty() ? QStringLiteral("目标会话设置或权限不允许派发。") : reason);
            record.insert("finishedAt", now());
        }
        runs[runs.size() - 1] = record;
        job.insert("runs", runs);
        m_jobs[i] = job;
        if (!save(error)) m_retry.start(5000);
        if (run.isEmpty() && error->isEmpty()) *error = QStringLiteral("自动化派发失败，请检查目标会话设置。");
        return record;
    }
    *error = QStringLiteral("自动化任务不存在。"); return {};
}
void AutomationService::tick()
{
    QStringList due;
    const auto time = QDateTime::currentDateTimeUtc();
    for (const auto &v : m_jobs) {
        const auto job = v.toObject();
        const auto next = QDateTime::fromString(job["next"].toString(), Qt::ISODateWithMs);
        if (job["enabled"].toInt() == EnabledJob && next.isValid() && next <= time) due.append(job["id"].toString());
    }
    QString firstError;
    int failures = 0;
    for (const auto &id : due) {
        QString error; trigger(id, true, &error);
        if (!error.isEmpty()) { ++failures; if (firstError.isEmpty()) firstError = error; }
    }
    if (!firstError.isEmpty() && firstError != m_lastSchedulerError)
        qCWarning(automationLog).noquote() << "周期任务派发失败，数量" << failures << "原因" << firstError;
    m_lastSchedulerError = firstError;
}
void AutomationService::ended(const QString &session, const QString &run, int state)
{
    bool changed = false;
    for (qsizetype i = 0; i < m_jobs.size(); ++i) {
        auto job = m_jobs[i].toObject();
        if (job["session"] != session) continue;
        auto runs = job["runs"].toArray();
        for (qsizetype j = 0; j < runs.size(); ++j) {
            auto record = runs[j].toObject();
            if (record["run"] != run || record["state"].toInt() != Running) continue;
            record.insert("state", state);
            record.insert("finishedAt", now());
            record.insert("reason", m_host.chat.agent()->executionInfo(session, run).value("detail").toString());
            runs[j] = record; changed = true;
        }
        job.insert("runs", runs); m_jobs[i] = job;
    }
    if (changed) { QString error; if (!save(&error)) m_retry.start(5000); }
}
}
