#include "conversation_feed.h"
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <utility>
namespace exagent::sharing {
namespace {
constexpr int MaxStreams = 8, MaxEvents = 512, MaxBytes = 4 * 1024 * 1024,
              PageBytes = 192 * 1024, PageRows = 32;
int size(const QJsonObject &o) {
  return int(QJsonDocument(o).toJson(QJsonDocument::Compact).size());
}
} // namespace
ConversationFeed::ConversationFeed(ChatController &chat, QObject *parent)
    : QObject(parent), m_chat(chat) {
  m_flush.setSingleShot(true);
  m_flush.setInterval(16);
  connect(&chat, &ChatController::conversationRowChanged, this,
          &ConversationFeed::changed);
  connect(&chat, &ChatController::conversationProjectionReset, this,
          &ConversationFeed::invalidate);
  connect(&m_flush, &QTimer::timeout, this, [this] {
    for (const auto &id : m_streams.keys())
      drain(id);
  });
}
void ConversationFeed::reset(Stream &s) {
  s.epoch = QUuid::createUuid().toString(QUuid::WithoutBraces);
  s.end = 0;
  s.bytes = 0;
  s.events.clear();
  s.dirty.clear();
  s.reset = false;
}
void ConversationFeed::changed(const QString &session, const QString &message) {
  auto it = m_streams.find(session);
  if (it == m_streams.end())
    return;
  // 未订阅会话不分配缓存；单帧突发过大时显式重置，不积累无界待处理对象。
  if (message.size() > 4096 || it->dirty.size() >= MaxEvents)
    it->reset = true;
  else
    it->dirty.insert(message);
  if (!m_flush.isActive())
    m_flush.start();
}
void ConversationFeed::invalidate(const QString &session) {
  auto it = m_streams.find(session);
  if (it == m_streams.end())
    return;
  it->reset = true;
  if (!m_flush.isActive())
    m_flush.start();
}
void ConversationFeed::drain(const QString &session) {
  auto it = m_streams.find(session);
  if (it == m_streams.end())
    return;
  auto &s = it.value();
  if (s.reset) {
    reset(s);
    return;
  }
  if (s.dirty.isEmpty())
    return;
  const auto pending = std::exchange(s.dirty, {});
  QString error;
  const auto rows = m_chat.conversationRows(session, pending, &error);
  if (!error.isEmpty()) {
    reset(s);
    return;
  }
  for (const auto &row : rows) {
    if (s.end >= 1000000000) {
      reset(s);
      return;
    }
    QJsonObject event{{"seq", ++s.end},
                      {"kind", int(Event::Upsert)},
                      {"row", row.toObject()}};
    const int bytes = size(event);
    if (bytes > PageBytes) {
      reset(s);
      return;
    }
    s.events.append(event);
    s.bytes += bytes;
    while (s.events.size() > MaxEvents || s.bytes > MaxBytes) {
      s.bytes -= size(s.events.first());
      s.events.removeFirst();
    }
  }
}
QJsonObject ConversationFeed::pull(const QString &session, const QString &epoch,
                                   int after, QString *error) {
  auto meta = m_chat.conversationInfo(session, error);
  if (!error->isEmpty())
    return {};
  if (!m_streams.contains(session)) {
    if (m_streams.size() >= MaxStreams) {
      auto oldest = m_streams.begin();
      for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
        if (it->touched < oldest->touched)
          oldest = it;
      m_streams.erase(oldest);
    }
    Stream s;
    reset(s);
    m_streams.insert(session, s);
  }
  drain(session);
  auto &s = m_streams[session];
  s.touched = ++m_reads;
  const int first =
      s.events.isEmpty() ? s.end + 1 : s.events.first()["seq"].toInt();
  if (epoch.isEmpty() || epoch != s.epoch || after < first - 1 ||
      after > s.end) {
    auto result = m_chat.conversationSnapshot(session, {}, error);
    if (!error->isEmpty())
      return {};
    result.insert("mode", int(Mode::Snapshot));
    result.insert("reason",
                  int(epoch.isEmpty()    ? ResetReason::Initial
                      : epoch != s.epoch ? ResetReason::EpochChanged
                                         : ResetReason::CursorExpired));
    result.insert("cursor", QJsonObject{{"epoch", s.epoch}, {"after", s.end}});
    result.insert("more", 0);
    return result;
  }
  // 同一消息在游标之后的多次变化合并为最新投影；游标只前进至实际扫描过的事件。
  QHash<QString, QJsonObject> latest;
  int cursor = after, bytes = 0;
  for (const auto &event : s.events) {
    const int seq = event["seq"].toInt();
    if (seq <= after)
      continue;
    const auto id = event["row"].toObject()["id"].toString();
    const bool existing = latest.contains(id);
    const int nextBytes =
        bytes + size(event) - (existing ? size(latest[id]) : 0);
    if ((!existing && latest.size() >= PageRows) || nextBytes > PageBytes)
      break;
    latest.insert(id, event);
    bytes = nextBytes;
    cursor = seq;
  }
  auto rows = latest.values();
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
    return a["seq"].toInt() < b["seq"].toInt();
  });
  QJsonArray events;
  for (const auto &row : rows)
    events.append(row);
  meta.insert("mode", int(Mode::Changes));
  meta.insert("events", events);
  meta.insert("cursor", QJsonObject{{"epoch", s.epoch}, {"after", cursor}});
  meta.insert("more", cursor < s.end ? 1 : 0);
  return meta;
}
} // namespace exagent::sharing
