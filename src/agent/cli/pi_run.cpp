#include "cli_run.h"
#include "domain/content/content_block.h"
#include "session_binding.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
namespace exagent::cli {
using State = content::TaskState;
static QString text(const QJsonObject &m) {
  QString result;
  for (const auto &v : m["content"].toArray())
    if (v.toObject()["type"] == "text")
      result += v.toObject()["text"].toString();
  return result;
}
class PiRun final : public Run {
public:
  using Run::Run;

private:
  bool guarded = false, stateReady = false, policyReady = false;
  int message = 0;
  QString item;
  int status = int(State::Complete);
  QJsonObject totals;
  QStringList arguments() override {
    const auto path = installPiGuard();
    if (path.isEmpty())
      return {"--invalid-ex-guard"};
    auto env = process.processEnvironment();
    env.insert("EX_NATIVE_WRITE", spec["write"].toBool() ? "1" : "0");
    env.insert("EX_NATIVE_FULL", spec["fullAccess"].toBool() ? "1" : "0");
    process.setProcessEnvironment(env);
    QStringList args{"--mode",
                     "rpc",
                     "--session",
                     source["file"].toString(),
                     "--no-extensions",
                     "--extension",
                     path};
    if (!spec["write"].toBool())
      args << "--tools"
           << "read,grep,find,ls";
    if (!spec["model"].toString().isEmpty())
      args << "--model" << spec["model"].toString();
    return args;
  }
  void started() override {
    send({{"id", "ex-policy"}, {"type", "set_auto_retry"}, {"enabled", false}});
    send({{"id", "ex-state"}, {"type", "get_state"}});
  }
  void prompt() {
    if (!guarded || !stateReady || !policyReady || prompted || stopping)
      return;
    if (!readyPrompt())
      return;
    send(
        {{"id", "ex-prompt"}, {"type", "prompt"}, {"message", spec["prompt"]}});
  }
  void frame(const QJsonObject &f) override {
    const auto type = f["type"].toString();
    if (type == "extension_ui_request") {
      if (f["method"] == "notify" && f["message"] == "EX_NATIVE_GUARD_V1") {
        guarded = true;
        prompt();
        return;
      }
      if (f["method"] == "confirm")
        requestApproval(f["id"].toString(), f,
                        f["title"].toString() + "\n" + f["message"].toString());
      else if (f["method"] == "select" || f["method"] == "input" ||
               f["method"] == "editor")
        send({{"type", "extension_ui_response"},
              {"id", f["id"]},
              {"cancelled", true}});
      return;
    }
    if (type == "response") {
      if (f["id"] == "ex-policy") {
        if (!f["success"].toBool()) {
          finish(int(State::Failed),
                 QStringLiteral("Pi不能确认关闭自动重试，未发送消息。"));
          return;
        }
        policyReady = true;
        prompt();
        return;
      }

      if (f["id"] == "ex-state") {
        const auto data = f["data"].toObject();
        if (!f["success"].toBool() || data["sessionId"] != source["id"] ||
            QFileInfo(data["sessionFile"].toString()).canonicalFilePath() !=
                QFileInfo(source["file"].toString()).canonicalFilePath()) {
          finish(int(State::Failed),
                 QStringLiteral("Pi恢复了不同的会话，未发送提示词。"));
          return;
        }
        stateReady = true;
        prompt();
      } else if (f["id"] == "ex-prompt" && !f["success"].toBool())
        finish(int(State::Failed), QStringLiteral("Pi拒绝接收消息，未重发。"));
      return;
    }
    if (!prompted)
      return;
    if (type == "message_start" &&
        f["message"].toObject()["role"] == "assistant")
      item = "assistant-" + QString::number(++message);
    if (type == "message_update") {
      const auto e = f["assistantMessageEvent"].toObject();
      if (e["type"] == "text_delta")
        output(item, e["delta"].toString(), false);
      else if (e["type"] == "thinking_start" || e["type"] == "thinking_end")
        emitEvent(
            {{"kind", "reasoningPhase"},
             {"state", int(e["type"] == "thinking_start" ? State::Running
                                                         : State::Complete)}});
    }
    if (type == "message_end") {
      const auto m = f["message"].toObject();
      if (m["role"] == "assistant") {
        if (item.isEmpty())
          item = "assistant-" + QString::number(++message);
        output(item, text(m), true);
        if (m["stopReason"] == "error")
          status = int(State::Failed);
        if (m["stopReason"] == "aborted")
          status = int(State::Cancelled);
        const auto u = m["usage"].toObject();
        for (const auto &pair :
             QList<QPair<QString, QString>>{{"input", "input_tokens"},
                                            {"output", "output_tokens"},
                                            {"totalTokens", "total_tokens"}})
          if (u.contains(pair.first))
            totals.insert(pair.second, totals[pair.second].toInteger() +
                                           u[pair.first].toInteger());
        if (!totals.isEmpty())
          emitEvent({{"kind", "usage"}, {"usage", totals}});
      }
      checkpoint();
    }
    if (type.startsWith("tool_execution_")) {
      const auto id = f["toolCallId"].toString();
      const auto tool = f["toolName"].toString();
      const auto args = f["args"].toObject();
      auto result =
          f[type == "tool_execution_update" ? "partialResult" : "result"]
              .toObject();
      output(id, text(result), true,
             tool + " · " +
                 QString::fromUtf8(
                     QJsonDocument(args).toJson(QJsonDocument::Compact)),
             "terminal");
      if (type == "tool_execution_end") {
        flush();
        emitEvent({{"kind", "itemEnd"},
                   {"item", id},
                   {"state", int(f["isError"].toBool() ? State::Failed
                                                       : State::Complete)}});
        checkpoint();
      }
    }
    // 不能把agent_end当最终结束；Pi可能还要重试或压缩，等待正式settled。
    if (type == "agent_settled")
      finish(status,
             status == int(State::Complete)
                 ? QStringLiteral("Pi执行完成")
                 : QStringLiteral("Pi本回合未成功完成，可查看原历史。"));
  }
  void approvalReply(const QJsonObject &r, bool allow) override {
    send({{"type", "extension_ui_response"},
          {"id", r["externalId"]},
          {"confirmed", allow}});
  }
  void abortProtocol() override {
    send({{"type", "clear_queue"}});
    send({{"type", "abort_retry"}});
    send({{"type", "abort"}});
  }
};
agent::RuntimeRun *createPi(QJsonObject s, QObject *o) {
  return new PiRun(std::move(s), o);
}
} // namespace exagent::cli
