#include "history_reader.h"
#include "plugins/session_history/history_provider.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
namespace exagent::terminalchat {
Snapshot readJsonl(const QVariantMap &source) {
  Snapshot out;
  const auto provider = source.value("provider").toInt();
  const auto name = provider == 2   ? QStringLiteral("Pi")
                    : provider == 3 ? QStringLiteral("Claude Code")
                                    : QString();
  const auto file = source.value("file").toString(),
             id = source.value("id").toString(),
             cwd = source.value("cwd").toString();
  bool found = false;
  // 仅接受已发现的主会话；不能借历史导入读取任意本机路径。
  for (const auto &v : discoverSessions(cwd)) {
    const auto row = v.toMap();
    if (row.value("agent") == name && row.value("id") == id &&
        row.value("file") == file) {
      found = true;
      break;
    }
  }
  QFile f(file);
  if (!found || QFileInfo(file).isSymLink() || f.size() > 32 * 1024 * 1024 ||
      !f.open(QIODevice::ReadOnly)) {
    out.error = QStringLiteral(
        "历史文件不在已发现范围内或超过32MiB。请保留原文件并选择其他会话。");
    return out;
  }
  QHash<QString, QJsonObject> nodes;
  QString leaf;
  while (!f.atEnd()) {
    const auto line = f.readLine(2 * 1024 * 1024 + 1);
    if (!line.endsWith('\n') && f.atEnd())
      break; // 正在写入的尾行留待下一次同步。
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(line, &error);
    if (f.pos() > 32 * 1024 * 1024 || line.size() > 2 * 1024 * 1024 ||
        error.error != QJsonParseError::NoError || !doc.isObject()) {
      out.error = QStringLiteral("历史条目无效，未导入部分内容。");
      return out;
    }
    auto row = doc.object();
    if (provider == 2 && row["type"] == "session") {
      if (row["id"] != id || row["cwd"] != cwd) {
        out.error = QStringLiteral("Pi历史身份不匹配。");
        return out;
      }
      continue;
    }
    if (provider == 3 &&
        (row["isSidechain"].toBool() || row["sessionId"] != id))
      continue;
    const auto key = row[provider == 2 ? "id" : "uuid"].toString();
    if (key.isEmpty())
      continue;
    if (nodes.size() >= 50000 && !nodes.contains(key)) {
      out.error = QStringLiteral("历史超过50000条，未截断导入。");
      return out;
    }
    nodes.insert(key, row);
    if (provider == 2 || row["message"].isObject())
      leaf = key;
  }
  // 沿最后活动分支回溯，避免把被回退的分支拼成一段对话。
  QStringList chain;
  QSet<QString> seen;
  while (!leaf.isEmpty() && nodes.contains(leaf)) {
    if (seen.contains(leaf)) {
      out.error = QStringLiteral("历史分支存在循环，未导入。");
      return out;
    }
    seen.insert(leaf);
    chain.prepend(leaf);
    leaf =
        nodes.value(leaf)[provider == 2 ? "parentId" : "parentUuid"].toString();
  }
  const auto owners = source.value("nativeOwners").toMap();
  QHash<QString, QString> before;
  QString next;
  for (auto i = chain.size(); i > 0; --i) {
    const auto key = chain[i - 1];
    if (owners.contains(key))
      next = owners.value(key).toString();
    else
      before.insert(key, next);
  }
  for (const auto &key : chain) {
    if (owners.contains(key))
      continue;
    const auto row = nodes.value(key);
    const auto message = row["message"].toObject();
    const auto role = message["role"].toString();
    if (role != "user" && role != "assistant" && role != "toolResult")
      continue;
    ChatMessage m;
    m.id = "terminal." +
           QString::fromLatin1(
               QCryptographicHash::hash((name + "/" + id + "/" + key).toUtf8(),
                                        QCryptographicHash::Sha256)
                   .toHex());
    m.role = role == "user"        ? MessageRole::User
             : role == "assistant" ? MessageRole::Assistant
                                   : MessageRole::Tool;
    const auto content = message["content"];
    if (content.isString())
      m.content = content.toString();
    else
      for (const auto &v : content.toArray()) {
        const auto part = v.toObject();
        const auto type = part["type"].toString();
        if (type == "text")
          m.content += part["text"].toString();
        else if (type == "tool_result") {
          m.role = MessageRole::Tool;
          if (part["is_error"].toBool())
            m.status = MessageStatus::Failed;
          if (part["content"].isString())
            m.content += part["content"].toString();
          else
            for (const auto &v : part["content"].toArray())
              if (v.toObject()["type"] == "text")
                m.content += v.toObject()["text"].toString();
        } else if (type == "image")
          m.content += QStringLiteral("\n[终端图片附件，未复制原始文件]\n");
      }
    m.startsTurn = m.role == MessageRole::User;
    if (message["isError"].toBool())
      m.status = MessageStatus::Failed;
    if (m.content.isEmpty())
      continue;
    if (row["timestamp"].isString())
      m.readTimestamp(row["timestamp"].toString());
    if (message["stopReason"] == "error")
      m.status = MessageStatus::Failed;
    else if (message["stopReason"] == "aborted")
      m.status = MessageStatus::Cancelled;
    m.activity = {{"nativeBeforeRun", before.value(key)},
                  {"terminalProvider", provider},
                  {"terminalThread", id},
                  {"terminalEntry", key}};
    out.messages.append(m);
  }
  return out;
}
} // namespace exagent::terminalchat
