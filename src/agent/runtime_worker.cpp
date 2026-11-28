#include "runtime_worker.h"
#include "codex/codex_run.h"
#include "cli/cli_run.h"
#include "domain/ipc/protocol.h"
#include "openai/responses_run.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
namespace exagent::agent {
Worker::Worker(QObject *parent, QHash<int, Factory> adapters)
    : QObject(parent), m_factories(std::move(adapters)) {
  registerAdapter(PiNative,cli::createPi);
  registerAdapter(ClaudeNative,cli::createClaude);
  registerAdapter(SystemCodex, [](QJsonObject p, QObject *owner) {
    return new codex::Run(std::move(p), owner);
  });
  registerAdapter(DedicatedCodex, [](QJsonObject p, QObject *owner) {
    return new codex::Run(std::move(p), owner);
  });
  registerAdapter(OpenAIResponses, [](QJsonObject p, QObject *owner) {
    return new openai::ResponsesRun(std::move(p), owner);
  });
}
void Worker::registerAdapter(int account, Factory factory) {
  if (account > 0 && !m_factories.contains(account))
    m_factories.insert(account, std::move(factory));
}
bool Worker::command(const QString &method, const QJsonObject &p,
                     QString *error) {
  const auto id = p["runId"].toString();
  const auto session = p["conversationId"].toString();
  auto *run = m_runs.value(id);
  if (method == "agent.steer") {
    if (run && run->ownsSession(session) &&
        run->steer(p["mailId"].toString(), p["turnId"].toString(),
                   p["text"].toString()))
      return true;
    *error = QStringLiteral("收件会话的回合已变化或不能接收消息。");
    return false;
  }
  if (method == "agent.stop" && run) {
    run->interrupt();
    return true;
  }
  if (method == "agent.approve" && run && p["allow"].isBool() &&
      run->decide(p["token"].toString(), p["allow"].toBool()))
    return true;
  const auto dir = QFileInfo(p["cwd"].toString());
  if (method != "agent.start" || !ipc::identifier(id) ||
      !ipc::identifier(session) || run || m_sessions.contains(session) ||
      m_runs.size() >= 8 || !dir.isAbsolute() || !dir.isDir() ||
      dir.canonicalFilePath() != dir.filePath() ||
      !m_factories.contains(p["account"].toInt()) ||
      (p["reconcile"].toBool() ? (p["threadId"].toString().isEmpty() ||
                                  p["turnId"].toString().isEmpty())
                               : p["prompt"].toString().isEmpty()) ||
      p["prompt"].toString().size() > 65536) {
    *error = QStringLiteral("运行、工作区或审批请求无效。");
    return false;
  }
  run = m_factories.value(p["account"].toInt())(p, this);
  m_runs.insert(id, run);
  m_sessions.insert(session);
  connect(run, &RuntimeRun::produced, this, [this](QJsonObject event) {
    const auto size =
        QJsonDocument(event).toJson(QJsonDocument::Compact).size();
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
  connect(run, &RuntimeRun::ended, this, [this, run, id, session] {
    m_runs.remove(id);
    m_sessions.remove(session);
    run->deleteLater();
  });
  QTimer::singleShot(0, run, &RuntimeRun::start);
  return true;
}
} // namespace exagent::agent
