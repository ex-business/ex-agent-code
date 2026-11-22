#include "content_block_adapters.h"
#include <QCryptographicHash>
#include <QJsonDocument>

namespace exagent {
using namespace content;
bool ExternalBlockAdapter::apply(const QString &method, const QJsonObject &payload, QString *error)
{
    if (method != "block.created" && method != "block.patch") {
        if (error) *error = QStringLiteral("外部内容方法未注册"); return false;
    }
    return m_store.applyEvent({BlockSource::External, method == "block.created" ? EventKind::Created : EventKind::Patch, payload}, error);
}
QVariantList ResourceBlockAdapter::project(const QString &conversationId, const QString &messageId, const QVariantList &parsed, QString *error)
{
    QVariantList result;
    int ordinal = 0;
    for (const auto &entry : parsed) {
        auto view = entry.toMap();
        const auto legacyKind = view.value("kind").toInt();
        if (legacyKind != 0 && legacyKind != 1) { if (error) *error = QStringLiteral("旧资源块类型无效"); return {}; }
        // Markdown临时kind0/1与正式BlockKind1/2必须显式转换，保留旧渲染接口。
        const auto kind = legacyKind == 1 ? BlockKind::Code : BlockKind::Text;
        QJsonObject payload{{"conversationId", conversationId}, {"kind", int(kind)}, {"text", view.value("text").toString()},
            {"data", QJsonObject{{"language", view.value("language").toString()}}}};
        // 静态快照以源身份、顺序和完整内容寻址；同一资源重载保持ID，显式编辑产生新快照。
        QJsonObject identity = payload; identity.insert("messageId", messageId); identity.insert("ordinal", ordinal++);
        const auto id = QStringLiteral("resource.") + QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        payload.insert("blockId", id);
        if (!m_store.applyEvent({BlockSource::Resource, EventKind::Snapshot, payload}, error)) return {};
        const auto *block = m_store.find(id);
        if (!block) { if (error) *error = QStringLiteral("资源快照未能进入缓存"); return {}; }
        view.insert("blockId", block->id); view.insert("contentMode", int(block->mode)); view.insert("sourceKind", int(block->source));
        view.insert("rendererKind", int(block->kind)); view.insert("revision", block->revision);
        result.append(view);
    }
    return result;
}
bool InternalBlockAdapter::prepare(ContentBlock block, QString *error)
{
    block.mode = ContentMode::Dynamic; block.source = BlockSource::Internal;
    return m_store.prepare(std::move(block), error);
}
bool InternalBlockAdapter::create(const QString &blockId, const QString &text, QString *error, const QJsonObject &data) { return send(blockId, text, TaskState::Running, true, error, data); }
bool InternalBlockAdapter::append(const QString &blockId, const QString &text, TaskState state, QString *error, const QJsonObject &data) { return send(blockId, text, state, false, error, data); }
bool InternalBlockAdapter::send(const QString &blockId, const QString &text, TaskState state, bool created, QString *error, const QJsonObject &data)
{
    const auto *block = m_store.find(blockId);
    if (!block || block->source != BlockSource::Internal || block->revision == 2147483647) {
        if (error) *error = QStringLiteral("内部任务块不存在、来源不符或版本已耗尽"); return false;
    }
    QJsonObject payload{{"blockId", block->id}, {"taskId", block->taskId}, {"conversationId", block->conversationId},
        {"revision", block->revision + 1}, {"taskState", int(state)}};
    if (created) { payload.insert("kind", int(block->kind)); payload.insert("title", block->title); payload.insert("text", text); }
    else payload.insert("appendText", text);
    if (!data.isEmpty()) payload.insert("data", data);
    return m_store.applyEvent({BlockSource::Internal, created ? EventKind::Created : EventKind::Patch, payload}, error);
}
}
