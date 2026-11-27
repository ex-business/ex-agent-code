#pragma once
#include "application/chat/chat_controller.h"
#include <QTimer>
namespace exagent::sharing {
// 回放缓存是显示投影，不替代持久聊天；缓存失效时从原历史生成新快照。
class ConversationFeed final : public QObject {
public:
  explicit ConversationFeed(ChatController &chat, QObject *parent = nullptr);
  QJsonObject pull(const QString &session, const QString &epoch, int after,
                   QString *error);

private:
  enum class Event { Upsert = 1 };
  enum class Mode { Snapshot = 1, Changes = 2 };
  enum class ResetReason { Initial = 1, CursorExpired = 2, EpochChanged = 3 };
  struct Stream {
    QString epoch;
    int end = 0, bytes = 0;
    quint64 touched = 0;
    QList<QJsonObject> events;
    QSet<QString> dirty;
    bool reset = false;
  };
  void changed(const QString &, const QString &);
  void invalidate(const QString &);
  void drain(const QString &);
  void reset(Stream &);
  ChatController &m_chat;
  QHash<QString, Stream> m_streams;
  QTimer m_flush;
  quint64 m_reads = 0;
};
} // namespace exagent::sharing
