#include "cli_run.h"
#include "domain/content/content_block.h"
#include "process_scope.h"
#include "session_binding.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
namespace exagent::cli {
using State = content::TaskState;
Run::Run(QJsonObject s, QObject *parent)
    : RuntimeRun(parent), spec(std::move(s)),
      source(spec["source"].toObject()) {
  scope = std::make_unique<ProcessScope>(this);
  publish.setSingleShot(true);
  publish.setInterval(32);
  connect(&publish, &QTimer::timeout, this, &Run::flush);
  deadline.setSingleShot(true);
  deadline.setInterval(30000);
  connect(&deadline, &QTimer::timeout, this, [this] {
    finish(int(State::Interrupted),
           QStringLiteral("Agent协议握手超时，未重放任务。"));
  });
  connect(&process, &QProcess::started, this, [this] {
    if (!scope->start(process.processId())) {
      finish(int(State::Failed),
             QStringLiteral("无法建立Agent进程隔离，未发送提示词。"));
      return;
    }
    deadline.start();
    started();
  });
  connect(&process, &QProcess::readyReadStandardOutput, this, &Run::read);
  connect(&process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
              finishing = true;
              finalState = int(State::Failed);
              finalDetail = QStringLiteral("无法启动所选Agent CLI。");
              complete();
            }
          });
  connect(
      &process, &QProcess::finished, this,
      [this](int code, QProcess::ExitStatus status) {
        read();
        if (!finishing) {
          finalState =
              stopping ? int(State::Cancelled) : int(State::Interrupted);
          finalDetail =
              QStringLiteral("Agent进程退出但未确认完整结果，未重发消息。");
        } else if (
            finalState == int(State::Complete) &&
            (code != 0 ||
             status !=
                 QProcess::
                     NormalExit)) { /* RPC按回合主动关闭时CLI可能使用信号退出；以已确认的协议结果为准。
                                     */
        }
        complete();
      });
}
Run::~Run() {
  scope->terminate();
  process.disconnect(this);
  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(1000);
  }
}
void Run::start() {
  QString error;
  const QFileInfo program(spec["program"].toString());
  if ((spec["account"] != agent::PiNative &&
       spec["account"] != agent::ClaudeNative) ||
      source["provider"].toInt() !=
          (spec["account"] == agent::PiNative ? Pi : Claude) ||
      !validSource(source, &error) || source["cwd"] != spec["cwd"] ||
      !program.isAbsolute() || !program.isExecutable()) {
    finishing = true;
    finalState = int(State::Failed);
    finalDetail =
        error.isEmpty() ? QStringLiteral("Agent程序或目录无效。") : error;
    complete();
    return;
  }
  lease = codex::claimThread(leaseKey(source));
  if (!lease) {
    finishing = true;
    finalState = int(State::Failed);
    finalDetail = QStringLiteral("Agent会话正在终端或另一聊天执行。");
    complete();
    return;
  }
  offset = QFileInfo(source["file"].toString()).size();
#ifdef Q_OS_UNIX
  process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
  process.setProgram(program.absoluteFilePath());
  process.setWorkingDirectory(spec["cwd"].toString());
  process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
  process.setStandardErrorFile(QProcess::nullDevice());
  process.setArguments(arguments());
  process.start();
}
void Run::send(const QJsonObject &frame) {
  if (done || finishing || process.state() != QProcess::Running)
    return;
  auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
  if (bytes.size() > 1024 * 1024 || process.write(bytes) != bytes.size())
    finish(int(State::Interrupted), QStringLiteral("Agent命令发送失败。"));
}
void Run::emitEvent(QJsonObject e) {
  if (done)
    return;
  e.insert("conversationId", spec["conversationId"]);
  e.insert("runId", spec["runId"]);
  emit produced(e);
}
void Run::output(const QString &item, const QString &text, bool replace,
                 const QString &title, const QString &kind) {
  if (done || item.isEmpty())
    return;
  auto e = outputs.value(item, QJsonObject{{"kind", "output"},
                                           {"item", item},
                                           {"type", kind},
                                           {"replace", replace},
                                           {"title", title}});
  if (!outputs.contains(item))
    order.append(item);
  if (replace) {
    e.insert("text", text);
    e.insert("replace", true);
  } else
    e.insert("text", e["text"].toString() + text);
  if (e["text"].toString().size() > 262144) {
    finish(
        int(State::Interrupted),
        QStringLiteral("单次Agent输出超过协议容量，原始记录仍在CLI历史中。"));
    return;
  }
  outputs.insert(item, e);
  if (!publish.isActive())
    publish.start();
}
void Run::flush() {
  const auto keys = order;
  order.clear();
  for (const auto &key : keys)
    emitEvent(outputs.take(key));
}
void Run::read() {
  buffer += process.readAllStandardOutput();
  while (buffer.contains('\n')) {
    const auto at = buffer.indexOf('\n');
    const auto line = buffer.left(at);
    buffer.remove(0, at + 1);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(line, &error);
    if (line.trimmed().isEmpty())
      continue;
    if (line.size() > 2 * 1024 * 1024 ||
        error.error != QJsonParseError::NoError || !doc.isObject()) {
      finish(int(State::Interrupted),
             QStringLiteral("Agent流式协议无效，未自动重试。"));
      return;
    }
    if (!finishing && !done)
      frame(doc.object());
  }
  if (buffer.size() > 2 * 1024 * 1024)
    finish(int(State::Interrupted), QStringLiteral("Agent协议帧超过容量。"));
}
bool Run::readyPrompt() {
  if (prompted || stopping || finishing)
    return false;
  prompted = true;
  deadline.stop();
  emitEvent({{"kind", "turn"}, {"turnId", spec["runId"]}});
  return true;
}
void Run::requestApproval(const QString &id, const QJsonObject &request,
                          const QString &detail) {
  if (id.isEmpty() || stopping || finishing)
    return;
  for (const auto &pending : approvals)
    if (pending["externalId"] == id)
      return;
  const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto record = request;
  record.insert("externalId", id);
  approvals.insert(token, record);
  emitEvent({{"kind", "approval"},
             {"token", token},
             {"detail", detail},
             {"turnId", spec["runId"]}});
}
bool Run::decide(const QString &token, bool allow) {
  if (!approvals.contains(token) || stopping || finishing)
    return false;
  const auto request = approvals.take(token);
  approvalReply(request, allow);
  emitEvent({{"kind", "approvalClosed"}, {"token", token}});
  return true;
}
void Run::closeApproval(const QString &id) {
  for (auto it = approvals.begin(); it != approvals.end();)
    if (it.value()["externalId"] == id) {
      auto token = it.key();
      it = approvals.erase(it);
      emitEvent({{"kind", "approvalClosed"}, {"token", token}});
    } else
      ++it;
}
void Run::interrupt() {
  if (done || stopping)
    return;
  stopping = true;
  abortProtocol();
  finish(int(State::Cancelled),
         QStringLiteral("已停止Agent，未重放工具或提示词。"));
}
void Run::checkpoint() {
  if (!prompted)
    return;
  QFile f(source["file"].toString());
  if (!f.open(QIODevice::ReadOnly) || f.size() < offset ||
      f.size() - offset > 16 * 1024 * 1024)
    return;
  f.seek(offset);
  QJsonArray ids;
  while (!f.atEnd()) {
    auto line = f.readLine(2 * 1024 * 1024 + 1);
    if (!line.endsWith('\n'))
      break;
    const auto row = QJsonDocument::fromJson(line).object();
    const auto id = row[source["provider"] == Pi ? "id" : "uuid"].toString();
    if (!id.isEmpty() && id.size() < 128)
      ids.append(id);
    if (ids.size() > 10000)
      return;
  }
  emitEvent({{"kind", "nativeCheckpoint"},
             {"sourceId", source["id"]},
             {"entries", ids}});
}
void Run::finish(int state, const QString &detail) {
  if (done || finishing)
    return;
  finishing = true;
  finalState = state;
  finalDetail = detail;
  deadline.stop();
  for (const auto &v : approvals.keys())
    emitEvent({{"kind", "approvalClosed"}, {"token", v}});
  approvals.clear();
  flush();
  if (process.state() == QProcess::NotRunning) {
    complete();
    return;
  }
  process.closeWriteChannel();
  QTimer::singleShot(350, this, [this] {
    if (process.state() != QProcess::NotRunning)
      process.terminate();
  });
  QTimer::singleShot(1500, this, [this] {
    if (process.state() != QProcess::NotRunning)
      process.kill();
  });
}
void Run::complete() {
  if (done)
    return;
  if (finalState == int(State::Complete))
    scope->disarm();
  else
    scope->terminate();
  if (!scope->quiet()) {
    QTimer::singleShot(50, this, [this] { complete(); });
    return;
  }
  deadline.stop();
  flush();
  checkpoint();
  emitEvent({{"kind", "end"}, {"state", finalState}, {"detail", finalDetail}});
  done = true;
  lease.reset();
  emit ended();
}
} // namespace exagent::cli
