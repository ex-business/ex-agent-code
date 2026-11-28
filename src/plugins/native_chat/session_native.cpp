#include "agent/cli/session_binding.h"
#include "agent/codex/thread_input.h"
#include "agent/runtime_run.h"
#include "application/agent/agent_controller.h"
#include "domain/content/content_block.h"
#include "plugins/design_mode/design_selection.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QUuid>
namespace exagent {
bool AgentController::nativeAvailable() const {
  const auto *s = find(m_selected);
  return s &&
         s->history.value("nativeAgent").toMap().value("status").toInt() == 1;
}
QVariantMap AgentController::nativeSettings() const {
  const auto *s = find(m_selected);
  auto value = s ? s->history.value("nativeAgent").toMap() : QVariantMap{};
  if (s && value.value("program").toString().isEmpty())
    value.insert("program", cli::executable(s->history.value("terminalSource")
                                                .toMap()
                                                .value("provider")
                                                .toInt()));
  return value;
}
bool AgentController::configureNative(const QString &program,
                                      const QString &model, bool write,
                                      bool full) {
  auto *s = find(m_selected);
  if (!s || !m_writable || !m_repository.writable() || model.size() > 128)
    return false;
  if (busy() || m_syncing.contains(s->id)) {
    emit failure(QStringLiteral("请等待当前运行或历史同步结束。"));
    return false;
  }
  const auto source =
      QJsonObject::fromVariantMap(s->history.value("terminalSource").toMap());
  QString error;
  if (!cli::validSource(source, &error) || !QFileInfo(program).isAbsolute() ||
      !QFileInfo(program).isExecutable()) {
    emit failure(error.isEmpty()
                     ? QStringLiteral("请选择有效的Agent可执行文件。")
                     : error);
    return false;
  }
  if (!codex::threadAvailable(cli::leaseKey(source))) {
    emit failure(QStringLiteral("请先退出占用会话的终端。"));
    return false;
  }
  const auto before = s->history;
  s->history.insert("nativeAgent", QVariantMap{{"status", 1},
                                               {"program", program},
                                               {"model", model.trimmed()},
                                               {"write", write || full},
                                               {"fullAccess", full}});
  if (!save()) {
    s->history = before;
    return false;
  }
  emit changed();
  return true;
}
bool AgentController::submitNative(const QString &session, const QString &text,
                                   const QVariantMap &review, bool worker) {
  auto *s = find(session);
  if (!s || !(worker ? m_repository.writable() : m_writable) ||
      !accountStorageWritable || !accountStorageWritable() ||
      m_active.contains(session) || m_syncing.contains(session))
    return false;
  const auto native = s->history.value("nativeAgent").toMap();
  const auto source =
      QJsonObject::fromVariantMap(s->history.value("terminalSource").toMap());
  QString error, prompt = text;
  if (native.value("status").toInt() != 1 ||
      !cli::validSource(source, &error)) {
    emit failure(error.isEmpty() ? QStringLiteral("请先启用此Agent的原生聊天。")
                                 : error);
    return false;
  }
  if (!codex::threadAvailable(cli::leaseKey(source))) {
    emit failure(QStringLiteral("此Agent会话仍由终端占用，请先退出终端。"));
    return false;
  }
  if (!review.isEmpty() || !design::pending(*s).isEmpty() ||
      m_contexts.contains(session)) {
    emit failure(QStringLiteral(
        "此CLI适配器目前只发送文本，请先移除附件和手动附带历史。"));
    return false;
  }
  if (!applyInputPolicies(*s, ChatInput, prompt, &error)) {
    emit failure(error);
    return false;
  }
  if (m_worker.state() == ipc::ServiceState::Failed && m_worker.processId() > 0)
    return false;
  const auto run = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto engine = source["provider"] == cli::Pi
                          ? QStringLiteral("Pi")
                          : QStringLiteral("Claude Code");
  QJsonObject spec{{"conversationId", session},
                   {"runId", run},
                   {"prompt", prompt},
                   {"cwd", source["cwd"]},
                   {"account", source["provider"] == cli::Pi
                                   ? agent::PiNative
                                   : agent::ClaudeNative},
                   {"source", source},
                   {"program", native.value("program").toString()},
                   {"model", native.value("model").toString()},
                   {"write", native.value("write").toBool()},
                   {"fullAccess", native.value("fullAccess").toBool()},
                   {"engine", engine}};
  ChatMessage user;
  user.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  user.content = text;
  user.startsTurn = true;
  ChatMessage marker;
  marker.id = run;
  marker.role = MessageRole::Notice;
  marker.status = MessageStatus::Running;
  marker.content = engine + QStringLiteral(" 正在准备执行");
  marker.activity = {{"serviceId", "codex-agent"},
                     {"taskState", int(content::TaskState::Created)},
                     {"cwd", source["cwd"].toString()},
                     {"account", spec["account"].toInt()},
                     {"nativeSource", source.toVariantMap()},
                     {"requestedModel", native.value("model")}};
  const auto before = *s;
  s->messages.append(user);
  s->messages.append(marker);
  if (!worker)
    s->draft.clear();
  s->updatedAt = QDateTime::currentDateTimeUtc();
  // 草稿、原生来源与派发意图原子保存后，才能交给后台执行，错误不自动重发。
  if (!save()) {
    *s = before;
    return false;
  }
  m_latestMarker.insert(session, s->messages.size() - 1);
  m_active.insert(session, {run, spec, {}, false, {}, {}, false, spec});
  emit rowChanged(session, user);
  emit rowChanged(session, marker);
  emit saved();
  emit changed();
  if (m_worker.state() == ipc::ServiceState::Ready)
    dispatch();
  else
    m_worker.start();
  return true;
}
} // namespace exagent
