#include "history_reader.h"
#include "agent/codex/codex_connection.h"
#include "agent/codex/thread_input.h"
#include "domain/content/content_block.h"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
namespace exagent::terminalchat {
Snapshot readCodex(const QVariantMap &b) {
  Snapshot out;
  const auto thread = b.value("threadId").toString();
  const auto cwd = b.value("boundCwd").toString();
  const auto account = codex::Account(b.value("boundAccount").toInt());
  if (QUuid(thread).isNull() || !QFileInfo(cwd).isDir() ||
      (account != codex::System && account != codex::Dedicated)) {
    out.error = QStringLiteral("终端历史的线程或目录绑定无效。");
    return out;
  }
  // 只读快照不争用终端输入租约；仅转换已结束回合，绝不resume/start或发送工具。
  codex::Connection connection;
  QEventLoop loop;
  QJsonObject snapshot;
  bool received = false;
  QObject::connect(&connection, &codex::Connection::ready, &loop, [&] {
    connection.send({{"id", 1},
                     {"method", "initialize"},
                     {"params", QJsonObject{{"clientInfo",
                                             QJsonObject{{"name", "ex_history"},
                                                         {"version", "1"}}}}}});
  });
  QObject::connect(&connection, &codex::Connection::failure, &loop,
                   [&](const QString &reason) {
                     out.error = reason;
                     loop.quit();
                   });
  QObject::connect(
      &connection, &codex::Connection::frame, &loop,
      [&](const QJsonObject &event) {
        const auto id = event["id"].toInt();
        if (id < 1 || id > 3)
          return;
        if (event.contains("error")) {
          out.error = QStringLiteral("引擎历史查询失败，未重放任务。");
          loop.quit();
          return;
        }
        if (!event.contains("result"))
          return;
        if (id == 1) {
          connection.send({{"method", "initialized"}});
          connection.send({{"id", 2},
                           {"method", "account/read"},
                           {"params", QJsonObject{{"refreshToken", false}}}});
        } else if (id == 2) {
          const auto owner = codex::identity(
              account, event["result"].toObject()["account"].toObject());
          if (owner.isEmpty() || owner != b.value("identity").toString()) {
            out.error =
                QStringLiteral("账号身份已变化，未导入其他账号的历史。");
            loop.quit();
            return;
          }
          connection.send({{"id", 3},
                           {"method", "thread/read"},
                           {"params", QJsonObject{{"threadId", thread},
                                                  {"includeTurns", true}}}});
        } else {
          snapshot = event["result"].toObject()["thread"].toObject();
          received = true;
          loop.quit();
        }
      });
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
    out.error = QStringLiteral("终端历史查询超时，可稍后重试。");
    loop.quit();
  });
  timer.start(15000);
  connection.start(account, cwd, b.value("accountHome").toString());
  loop.exec();
  if (!out.error.isEmpty())
    return out;
  if (!received || snapshot["id"] != thread || !snapshot["turns"].isArray()) {
    out.error = QStringLiteral("引擎返回了不匹配的线程历史。");
    return out;
  }
  const auto knownList = b.value("knownTurns").toStringList();
  const QSet<QString> known(knownList.cbegin(), knownList.cend());
  const auto turns = snapshot["turns"].toArray();
  QHash<QString, QString> before;
  QString next;
  for (auto i = turns.size(); i > 0; --i) {
    const auto tid = turns[i - 1].toObject()["id"].toString();
    if (known.contains(tid))
      next = tid;
    else
      before.insert(tid, next);
  }
  for (const auto &v : snapshot["turns"].toArray()) {
    const auto turn = v.toObject();
    const auto tid = turn["id"].toString();
    const auto state = turn["status"].toString();
    if (tid.isEmpty() || known.contains(tid) || state == "inProgress")
      continue;
    if (state != "completed" && state != "interrupted" && state != "failed") {
      out.error = QStringLiteral("未知回合状态，未保存部分历史。");
      out.messages.clear();
      return out;
    }
    int position = 0;
    for (const auto &x : turn["items"].toArray()) {
      const auto item = x.toObject();
      const auto type = item["type"].toString();
      ChatMessage m;
      const auto itemKey = item["id"].toString().isEmpty()
                               ? QString::number(position)
                               : item["id"].toString();
      ++position;
      m.id =
          "terminal." +
          QString::fromLatin1(QCryptographicHash::hash(
                                  (thread + "/" + tid + "/" + itemKey).toUtf8(),
                                  QCryptographicHash::Sha256)
                                  .toHex());
      m.status = state == "failed"        ? MessageStatus::Failed
                 : state == "interrupted" ? MessageStatus::Cancelled
                                          : MessageStatus::Complete;
      m.activity = {{"terminalBeforeTurn", before.value(tid)},
                    {"terminalThread", thread},
                    {"terminalTurn", tid},
                    {"terminalProvider", 1},
                    {"profileId", b.value("profileId")}};
      if (type == "userMessage") {
        m.role = MessageRole::User;
        m.startsTurn = true;
        for (const auto &c : item["content"].toArray()) {
          const auto part = c.toObject();
          if (part["type"] == "text")
            m.content += part["text"].toString();
          else if (part["type"] == "image" || part["type"] == "localImage")
            m.content += QStringLiteral("\n[终端图片附件，未复制原始文件]\n");
        }
      } else if (type == "agentMessage") {
        m.role = MessageRole::Assistant;
        m.content = item["text"].toString();
      } else if (type == "commandExecution") {
        m.role = MessageRole::Tool;
        m.title = item["command"].toString();
        m.content = item["aggregatedOutput"].toString();
        m.activity.insert("kind", 2);
        m.activity.insert("summary", m.title);
        m.activity.insert("blockKind", int(content::BlockKind::Terminal));
        m.activity.insert("exitCode", item["exitCode"].toInt());
        if (item["status"] == "failed" || item["status"] == "declined" ||
            item["exitCode"].toInt() != 0)
          m.status = MessageStatus::Failed;
      } else if (type == "fileChange") {
        m.role = MessageRole::Tool;
        m.title = QStringLiteral("文件改动");
        for (const auto &c : item["changes"].toArray()) {
          const auto change = c.toObject();
          m.content += change["path"].toString() + '\n' +
                       change["diff"].toString() + '\n';
        }
        m.activity.insert("kind", 2);
        m.activity.insert("summary", m.title);
        m.activity.insert("blockKind", int(content::BlockKind::Diff));
        if (item["status"] == "failed" || item["status"] == "declined")
          m.status = MessageStatus::Failed;
      } else
        continue; // 不读取推理、认证信息和未识别的私有条目。
      if (!m.content.isEmpty() || !m.title.isEmpty())
        out.messages.append(m);
    }
  }
  return out;
}
} // namespace exagent::terminalchat
