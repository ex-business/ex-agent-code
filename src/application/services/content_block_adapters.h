#pragma once
#include "content_block_store.h"
#include <QVariantList>

namespace exagent {
class ExternalBlockAdapter {
public:
    explicit ExternalBlockAdapter(ContentBlockStore &store) : m_store(store) {}
    bool apply(const QString &method, const QJsonObject &payload, QString *error);
private:
    ContentBlockStore &m_store;
};
class ResourceBlockAdapter {
public:
    explicit ResourceBlockAdapter(ContentBlockStore &store) : m_store(store) {}
    QVariantList project(const QString &conversationId, const QString &messageId, const QVariantList &parsed, QString *error);
private:
    ContentBlockStore &m_store;
};
class InternalBlockAdapter {
public:
    explicit InternalBlockAdapter(ContentBlockStore &store) : m_store(store) {}
    bool prepare(ContentBlock block, QString *error);
    bool create(const QString &blockId, const QString &text, QString *error, const QJsonObject &data = {});
    bool append(const QString &blockId, const QString &text, content::TaskState state, QString *error, const QJsonObject &data = {});
private:
    bool send(const QString &blockId, const QString &text, content::TaskState state, bool created, QString *error, const QJsonObject &data);
    ContentBlockStore &m_store;
};
}
