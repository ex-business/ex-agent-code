#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantList>

namespace exagent {

// 自有消息类型使用数字契约；未来在引擎边界映射第三方协议。
enum class MessageRole : int { User = 1, Assistant = 2, Tool = 3, Notice = 4 };
enum class MessageStatus : int { Complete = 0, Running = 1, Failed = 2, Cancelled = 3 };

struct ChatMessage {
    QString id;
    MessageRole role = MessageRole::User;
    QString content;
    QString title;
    MessageStatus status = MessageStatus::Complete;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
    bool expanded = false;
    QVariantList files;
    QVariantMap activity;
    QVariantList attachments;
    QString originSession;
    QString originMessage;
    bool startsTurn = false;
    bool operator==(const ChatMessage &) const = default;
};

struct ChatSession {
    QString id;
    QString title;
    QString draft;
    QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    QList<ChatMessage> messages;
    QString projectId;
    QString workspaceId;
    QVariantList transfers;
    bool operator==(const ChatSession &) const = default;
};

} // namespace exagent
