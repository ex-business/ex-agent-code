#include "cli_run.h"
#include "domain/content/content_block.h"
#include <QJsonArray>
#include <QJsonDocument>
namespace exagent::cli {
using State = content::TaskState;
class ClaudeRun final : public Run {
public:
  using Run::Run;

private:
  QString message;
  QHash<int, QString> blocks;
  bool initialized = false;
  QStringList arguments() override {
    QStringList a{"--print",
                  "--input-format",
                  "stream-json",
                  "--output-format",
                  "stream-json",
                  "--verbose",
                  "--include-partial-messages",
                  "--replay-user-messages",
                  "--permission-prompt-tool",
                  "stdio",
                  "--permission-mode",
                  "default",
                  "--resume",
                  source["id"].toString(),
                  "--setting-sources",
                  "",
                  "--strict-mcp-config",
                  "--mcp-config",
                  R"({"mcpServers":{}})",
                  "--tools",
                  spec["write"].toBool() ? "Read,Grep,Glob,Bash,Write,Edit"
                                         : "Read,Grep,Glob"};
    if (!spec["model"].toString().isEmpty())
      a << "--model" << spec["model"].toString();
    return a;
  }
  void controlReply(const QString &id, const QJsonObject &response) {
    send({{"type", "control_response"},
          {"response", QJsonObject{{"subtype", "success"},
                                   {"request_id", id},
                                   {"response", response}}}});
  }
  void started() override {
    QJsonObject matcher{{"matcher", ".*"},
                        {"hookCallbackIds", QJsonArray{"ex-permission"}},
                        {"onFailure", "block"}};
    send({{"type", "control_request"},
          {"request_id", "ex-init"},
          {"request",
           QJsonObject{
               {"subtype", "initialize"},
               {"hooks", QJsonObject{{"PreToolUse", QJsonArray{matcher}}}}}}});
  }
  bool readTool(const QString &tool) {
    return tool == "Read" || tool == "Grep" || tool == "Glob";
  }
  void frame(const QJsonObject &f) override {
    const auto type = f["type"].toString();
    if (type == "control_response") {
      const auto r = f["response"].toObject();
      if (r["request_id"] != "ex-init")
        return;
      if (r["subtype"] != "success" ||
          !r["response"].toObject()["hook_on_failure_supported"].toBool() ||
          r["response"].toObject()["hooks_applied"] != true) {
        finish(
            int(State::Failed),
            QStringLiteral(
                "Claude CLI不能确认强制审批钩子，请更新CLI；未发送提示词。"));
        return;
      }
      initialized = true;
      if (!readyPrompt())
        return;
      send({{"type", "user"},
            {"uuid", spec["runId"]},
            {"session_id", source["id"]},
            {"message",
             QJsonObject{{"role", "user"}, {"content", spec["prompt"]}}},
            {"parent_tool_use_id", QJsonValue::Null}});
      return;
    }
    if (type == "control_request") {
      const auto request = f["request"].toObject();
      const auto id = f["request_id"].toString();
      const auto subtype = request["subtype"].toString();
      if (subtype != "can_use_tool" &&
          (subtype != "hook_callback" ||
           request["callback_id"] != "ex-permission")) {
        send({{"type", "control_response"},
              {"response", QJsonObject{{"subtype", "error"},
                                       {"request_id", id},
                                       {"error", "不支持的宿主请求"}}}});
        return;
      }
      auto input =
          subtype == "hook_callback" ? request["input"].toObject() : request;
      const auto tool = input["tool_name"].toString();
      const auto args =
          input[subtype == "hook_callback" ? "tool_input" : "input"].toObject();
      auto record = request;
      record.insert("externalId", id);
      record.insert("toolInput", args);
      if (!spec["write"].toBool() && !readTool(tool)) {
        approvalReply(record, false);
        return;
      }
      // 每次非只读操作由EX审批；不向CLI写入持久允许规则。
      if (spec["fullAccess"].toBool()) {
        approvalReply(record, true);
        return;
      }
      requestApproval(id, record,
                      tool + "\n" +
                          QString::fromUtf8(QJsonDocument(args).toJson(
                              QJsonDocument::Indented)));
      return;
    }
    if (type == "control_cancel_request") {
      closeApproval(f["request_id"].toString());
      return;
    }
    if (!initialized)
      return;
    if (f.contains("session_id") &&
        f["session_id"].toString() != source["id"].toString()) {
      finish(int(State::Interrupted),
             QStringLiteral("Claude返回了不同会话，已停止。"));
      return;
    }
    if (type == "stream_event") {
      if (!f["parent_tool_use_id"].isNull() &&
          !f["parent_tool_use_id"].toString().isEmpty())
        return;
      const auto e = f["event"].toObject();
      const auto t = e["type"].toString();
      const auto ix = e["index"].toInt();
      if (t == "message_start") {
        message = e["message"].toObject()["id"].toString();
        blocks.clear();
      } else if (t == "content_block_delta" &&
                 e["delta"].toObject()["type"] == "text_delta") {
        blocks[ix] += e["delta"].toObject()["text"].toString();
        output(message + "." + QString::number(ix), blocks[ix], true);
      }
      return;
    }
    if (type == "assistant") {
      if (!f["parent_tool_use_id"].isNull() &&
          !f["parent_tool_use_id"].toString().isEmpty())
        return;
      const auto m = f["message"].toObject();
      const auto id = m["id"].toString();
      int i = 0;
      for (const auto &v : m["content"].toArray()) {
        const auto b = v.toObject();
        if (b["type"] == "text")
          output(id + "." + QString::number(i), b["text"].toString(), true);
        else if (b["type"] == "tool_use")
          output(b["id"].toString(), "", true,
                 b["name"].toString() + " · " +
                     QString::fromUtf8(QJsonDocument(b["input"].toObject())
                                           .toJson(QJsonDocument::Compact)),
                 "terminal");
        ++i;
      }
      checkpoint();
    }
    if (type == "user") {
      for (const auto &v : f["message"].toObject()["content"].toArray()) {
        const auto b = v.toObject();
        if (b["type"] != "tool_result")
          continue;
        QString text;
        if (b["content"].isString())
          text = b["content"].toString();
        else
          for (const auto &x : b["content"].toArray())
            if (x.toObject()["type"] == "text")
              text += x.toObject()["text"].toString();
        output(b["tool_use_id"].toString(), text, true, {}, "terminal");
        flush();
        emitEvent({{"kind", "itemEnd"},
                   {"item", b["tool_use_id"]},
                   {"state", int(b["is_error"].toBool() ? State::Failed
                                                        : State::Complete)}});
      }
      checkpoint();
    }
    if (type == "result") {
      const auto u = f["usage"].toObject();
      QJsonObject usage;
      for (const auto &k : {"input_tokens", "output_tokens"})
        if (u.contains(k))
          usage.insert(k, u[k]);
      if (u.contains("input_tokens") && u.contains("output_tokens"))
        usage.insert("total_tokens", u["input_tokens"].toInteger() +
                                         u["output_tokens"].toInteger());
      if (!usage.isEmpty())
        emitEvent({{"kind", "usage"}, {"usage", usage}});
      finish(int(f["is_error"].toBool() || f["subtype"] != "success"
                     ? State::Failed
                     : State::Complete),
             f["is_error"].toBool()
                 ? QStringLiteral("Claude执行失败，请检查CLI登录与服务状态。")
                 : QStringLiteral("Claude执行完成"));
    }
  }
  void approvalReply(const QJsonObject &r, bool allow) override {
    QJsonObject reply;
    if (r["subtype"] == "hook_callback")
      reply = {
          {"hookSpecificOutput",
           QJsonObject{{"hookEventName", "PreToolUse"},
                       {"permissionDecision", allow ? "allow" : "deny"},
                       {"permissionDecisionReason",
                        allow ? "用户允许此次操作" : "用户拒绝或当前只读"}}}};
    else
      reply = allow ? QJsonObject{{"behavior", "allow"},
                                  {"updatedInput", r["toolInput"]}}
                    : QJsonObject{{"behavior", "deny"},
                                  {"message", "用户拒绝或当前只读"}};
    controlReply(r["externalId"].toString(), reply);
  }
  void abortProtocol() override {
    send({{"type", "control_request"},
          {"request_id", "ex-stop"},
          {"request", QJsonObject{{"subtype", "interrupt"}}}});
  }
};
agent::RuntimeRun *createClaude(QJsonObject s, QObject *o) {
  return new ClaudeRun(std::move(s), o);
}
} // namespace exagent::cli
