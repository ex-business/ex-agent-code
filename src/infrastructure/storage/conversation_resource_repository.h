#pragma once
#include "domain/chat/chat_message.h"
#include <QVariantList>

namespace exagent {
struct ConversationResource {
    QString id;
    QString title;
    int messageCount = 0;
    int pageCount = 0;
    QVariantMap changeSummary;
    int activityStatus = 0;
    QVariantMap source;
};

// 每个会话一个目录，清单与消息分页分离，附件留在同目录 attachments 下。
class ConversationResourceRepository {
public:
    QString rootPath() const;
    QVariantList list() const;
    QVariantList navigation(QString *error = nullptr) const;
    QVariantMap navigationInfo() const;
    ConversationResource manifest(const QString &id, QString *error) const;
    QList<ChatMessage> page(const ConversationResource &resource, int number, QString *error, bool requireAllAttachments = false) const;
};
}
