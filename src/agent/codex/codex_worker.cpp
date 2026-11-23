#include "codex_worker.h"
#include "domain/ipc/protocol.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
namespace exagent::codex {
bool Worker::command(const QString &method, const QJsonObject &p, QString *error)
{
    const auto id = p["runId"].toString();
    const auto session = p["conversationId"].toString();
    auto *run = m_runs.value(id);
    if (method == "agent.stop" && run) {
        run->interrupt();
        return true;
    }
    if (method == "agent.approve" && run && p["allow"].isBool()
        && run->decide(p["token"].toString(), p["allow"].toBool()))
        return true;
    const auto dir = QFileInfo(p["cwd"].toString());
    if (method != "agent.start" || !ipc::identifier(id) || !ipc::identifier(session) || run
        || m_sessions.contains(session) || m_runs.size() >= 8 || !dir.isAbsolute() || !dir.isDir()
        || dir.canonicalFilePath() != dir.filePath()
        || (p["account"].toInt() != System && p["account"].toInt() != Dedicated)
        || (p["reconcile"].toBool() ? (p["threadId"].toString().isEmpty() || p["turnId"].toString().isEmpty())
                                    : p["prompt"].toString().isEmpty())
        || p["prompt"].toString().size() > 65536) {
        *error = QStringLiteral("运行、工作区或审批请求无效。");
        return false;
    }
    run = new Run(p, this);
    m_runs.insert(id, run);
    m_sessions.insert(session);
    connect(run, &Run::produced, this, [this](QJsonObject event) {
        const auto size = QJsonDocument(event).toJson(QJsonDocument::Compact).size();
        if (queuedBytes + size > ipc::MaxQueue || events.size() >= 2048) {
            // 宿主长期离线时以中断结束，避免静默丢掉输出后仍报告完成。
            QCoreApplication::exit(3);
            return;
        }
        event.insert("_bytes", size);
        queuedBytes += size;
        events.enqueue(event);
        emit available();
    });
    connect(run, &Run::ended, this, [this, run, id, session] {
        m_runs.remove(id);
        m_sessions.remove(session);
        run->deleteLater();
    });
    QTimer::singleShot(0, run, &Run::start);
    return true;
}
}
