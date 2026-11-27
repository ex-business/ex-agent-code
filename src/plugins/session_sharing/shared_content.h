#pragma once
#include "application/chat/chat_controller.h"
namespace exagent::sharing {
QString contentRevision(const ChatMessage &message);
QJsonArray attachmentSummary(const ChatMessage &message);
// 只接收已由接入层绑定的会话ID；不接受客户端文件路径或任意URL。
class SharedContent : public QObject {
public:
  explicit SharedContent(ChatController &chat) : m_chat(chat) {}
  QJsonObject text(const QString &session, const QJsonObject &params,
                   QString *error) const;
  QJsonObject image(const QString &session, const QJsonObject &params,
                    QString *error);

private:
  struct ImageSnapshot {
    QByteArray bytes;
    QString type;
    quint64 touched = 0;
  };
  ChatController &m_chat;
  QHash<QString, ImageSnapshot> m_images;
  QSet<QString> m_pending;
  QHash<QString, QPair<QString, qint64>> m_failures;
  quint64 m_reads = 0;
  qint64 m_bytes = 0;
};
} // namespace exagent::sharing
