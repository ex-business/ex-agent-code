#pragma once
#include "domain/chat/chat_message.h"
#include <QImage>

namespace exagent::design {
struct SelectionResult { QVariantMap selection; QString error; };
SelectionResult capture(const QVariantMap &data, const QImage &surface, const QString &directory);
QVariantMap pending(const ChatSession &session);
void setPending(ChatSession &session, const QVariantMap &selection);
QString apply(const QVariantMap &selection, const QString &directory, ChatMessage &message, QString &imagePath);
}
