#include "content_block_store.h"
#include <QJsonDocument>
#include <QJsonArray>

namespace exagent {
using namespace content;
namespace {
qsizetype cost(const ContentBlock &block) { return block.text.size() * 2 + QJsonDocument(block.data).toJson(QJsonDocument::Compact).size(); }
bool reject(QString *error, const QString &reason) { if (error) *error = reason; return false; }
bool validPayload(BlockKind kind, const QJsonObject &data)
{
    if (data.isEmpty()) return true; // 允许运行中尚未产生媒体数据的占位快照。
    if (kind == BlockKind::Chart) {
        if (!data.value("series").isArray() || data.value("series").toArray().size() > 64) return false;
        for (const auto &point : data.value("series").toArray()) {
            const auto p = point.toObject();
            if (!point.isObject() || !p.value("label").isString() || p.value("label").toString().size() > 200
                || !p.value("value").isDouble() || !qIsFinite(p.value("value").toDouble()) || p.value("value").toDouble() < 0 || p.value("value").toDouble() > 1e12) return false;
        }
    }
    if (kind == BlockKind::Image && !identifier(data.value("resourceId"))) return false;
    if (kind == BlockKind::ImageSequence) {
        if (!data.value("frames").isArray() || data.value("frames").toArray().size() > 500 || !integer(data.value("durationMs"), 20, 10000)) return false;
        for (const auto &frame : data.value("frames").toArray())
            if (!frame.isObject() || !identifier(frame.toObject().value("resourceId"))) return false;
    }
    return true;
}

}
const ContentBlock *ContentBlockStore::find(const QString &id) const { const auto it = m_blocks.constFind(id); return it == m_blocks.cend() ? m_static.object(id) : &it.value(); }
QStringList ContentBlockStore::taskBlocks(const QString &id) const { return m_tasks.value(id); }
const ContentBlock *ContentBlockStore::task(const QString &id) const
{
    const auto ids = taskBlocks(id);
    // 旧单块API只在无歧义时返回，任务总状态必须使用归约结果。
    return ids.size() == 1 ? find(ids.first()) : nullptr;
}
int ContentBlockStore::taskState(const QString &id) const
{
    const auto ids = taskBlocks(id);
    if (ids.isEmpty()) return -1;
    bool created = false, running = false, interrupted = false, failed = false, cancelled = false, complete = false;
    for (const auto &blockId : ids) {
        const auto *block = find(blockId);
        if (!block) return int(TaskState::Interrupted);
        switch (block->state) {
        case TaskState::Created: created = true; break;
        case TaskState::Running: running = true; break;
        case TaskState::Interrupted: interrupted = true; break;
        case TaskState::Failed: failed = true; break;
        case TaskState::Cancelled: cancelled = true; break;
        case TaskState::Complete: complete = true; break;
        }
    }
    // 所有块完成才算任务完成；尚有活动块时先保留运行态，终态优先未知、失败、取消。
    if (running || (created && (interrupted || failed || cancelled || complete))) return int(TaskState::Running);
    if (created) return int(TaskState::Created);
    if (interrupted) return int(TaskState::Interrupted);
    if (failed) return int(TaskState::Failed);
    if (cancelled) return int(TaskState::Cancelled);
    return int(TaskState::Complete);
}
QVariantMap ContentBlockStore::snapshot(const QString &id) const
{
    const auto *block = find(id);
    if (!block) return {};
    QVariantMap result{{"blockId", block->id}, {"rendererKind", int(block->kind)}, {"contentMode", int(block->mode)},
        {"sourceKind", int(block->source)}, {"revision", block->revision}, {"title", block->title}, {"text", block->text}, {"data", block->data.toVariantMap()}};
    if (block->mode == ContentMode::Dynamic) { result.insert("taskId", block->taskId); result.insert("taskState", int(block->state)); }
    return result;
}
bool ContentBlockStore::prepare(ContentBlock block, QString *error)
{
    if (block.mode != ContentMode::Dynamic || (block.source != BlockSource::Internal && block.source != BlockSource::External)
        || !identifier(block.id) || !identifier(block.taskId) || !identifier(block.conversationId)
        || block.title.size() > 200 || block.revision != 0 || block.state != TaskState::Created
        || block.text.size() > 512 * 1024 || m_bytes + cost(block) > 32 * 1024 * 1024)
        return reject(error, QStringLiteral("内容块初始状态、身份或容量无效"));
    if (m_blocks.size() >= 256 || m_blocks.contains(block.id) || m_static.contains(block.id)) return reject(error, QStringLiteral("内容块容量已满或身份重复"));
    const auto siblings = m_tasks.value(block.taskId);
    if (!siblings.isEmpty()) {
        const auto *first = find(siblings.first());
        if (!first || first->conversationId != block.conversationId || first->source != block.source)
            return reject(error, QStringLiteral("同一任务的所有块必须属于相同会话和来源"));
        // 任务开始后不能偷偷追加新块，使已经完成的任务重新变成运行中。
        if (taskState(block.taskId) != int(TaskState::Created)) return reject(error, QStringLiteral("任务开始前必须一次登记全部内容块"));
    }
    m_tasks[block.taskId].append(block.id);
    m_bytes += cost(block); const auto id = block.id;
    m_blocks.insert(id, std::move(block)); emit blockChanged(id); return true;
}
bool ContentBlockStore::apply(const QString &method, const QJsonObject &event, QString *error)
{
    // 兼容现有v1调用方，生产路径也通过同一个归约入口校验。
    if (method != "block.created" && method != "block.patch") return reject(error, QStringLiteral("不支持的外部内容方法"));
    return applyEvent({BlockSource::External, method == "block.created" ? EventKind::Created : EventKind::Patch, event}, error);
}
bool ContentBlockStore::applyEvent(const BlockEvent &input, QString *error)
{
    if (input.kind == EventKind::Snapshot) return applySnapshot(input, error);
    const auto &event = input.payload;
    if ((input.source != BlockSource::Internal && input.source != BlockSource::External)
        || (input.kind != EventKind::Created && input.kind != EventKind::Patch) || !identifier(event.value("blockId"))
        || !identifier(event.value("taskId")) || !identifier(event.value("conversationId"))
        || !integer(event.value("revision"), 1, 2147483647) || !integer(event.value("taskState"), 0, 4))
        return reject(error, QStringLiteral("内容事件字段无效"));
    const auto id = event.value("blockId").toString();
    const auto *previous = find(id);
    if (!previous || previous->mode != ContentMode::Dynamic || previous->source != input.source || previous->taskId != event.value("taskId").toString() || previous->conversationId != event.value("conversationId").toString())
        return reject(error, QStringLiteral("内容事件超出已授权会话或任务"));
    if (event.value("revision").toInt() != previous->revision + 1) return reject(error, QStringLiteral("内容版本不连续"));
    auto next = *previous;
    const bool created = input.kind == EventKind::Created;
    if (created) {
        if (next.revision != 0 || !integer(event.value("kind"), 1, 7) || !event.value("title").isString()
            || event.value("title").toString().size() > 200 || !event.value("text").isString())
            return reject(error, QStringLiteral("内容块创建字段无效或重复创建"));
        next.kind = BlockKind(event.value("kind").toInt()); next.title = event.value("title").toString();
        next.text = event.value("text").toString();
    } else {
        if (next.revision == 0 || (next.state != TaskState::Running && next.state != TaskState::Interrupted && next.state != TaskState::Created))
            return reject(error, QStringLiteral("未创建或已结束的内容块不能继续修改"));
        if (!event.value("appendText").isString()) return reject(error, QStringLiteral("内容增量必须为文本"));
        next.text += event.value("appendText").toString();
    }
    if (event.contains("data")) {
        if (!event.value("data").isObject()) return reject(error, QStringLiteral("结构化内容必须为对象"));
        next.data = event.value("data").toObject();
    }
    if (!validPayload(next.kind, next.data)) return reject(error, QStringLiteral("内容类型与结构化数据不匹配或超过显示预算"));
    next.state = TaskState(event.value("taskState").toInt()); next.revision = event.value("revision").toInt();
    if (next.state == TaskState::Created && !created) return reject(error, QStringLiteral("运行中的任务不能退回创建状态"));
    const auto nextBytes = m_bytes - cost(*previous) + cost(next);
    if (next.text.size() > 512 * 1024 || nextBytes > 32 * 1024 * 1024) return reject(error, QStringLiteral("内容容量超限，保留当前快照并暂停接收"));
    m_blocks[id] = std::move(next); m_bytes = nextBytes;
    emit blockChanged(id); return true;
}
bool ContentBlockStore::applySnapshot(const BlockEvent &input, QString *error)
{
    const auto &event = input.payload;
    if (input.source != BlockSource::Resource || !identifier(event.value("blockId")) || !identifier(event.value("conversationId"))
        || !integer(event.value("kind"), 1, 7) || !event.value("text").isString() || event.value("text").toString().size() > 512 * 1024
        || (event.contains("data") && !event.value("data").isObject()) || event.contains("taskId") || event.contains("taskState"))
        return reject(error, QStringLiteral("静态资源快照字段无效"));
    ContentBlock block;
    block.id = event.value("blockId").toString(); block.conversationId = event.value("conversationId").toString();
    if (m_blocks.contains(block.id)) return reject(error, QStringLiteral("静态快照不能覆盖动态任务"));
    block.mode = ContentMode::Static; block.source = BlockSource::Resource;
    block.kind = BlockKind(event.value("kind").toInt()); block.text = event.value("text").toString(); block.data = event.value("data").toObject();
    if (!validPayload(block.kind, block.data)) return reject(error, QStringLiteral("静态资源内容格式无效"));
    block.revision = 1;
    if (const auto *old = m_static.object(block.id)) {
        if (old->conversationId != block.conversationId) return reject(error, QStringLiteral("静态资源不能变更会话归属"));
        if (old->text == block.text && old->kind == block.kind && old->data == block.data) return true;
        return reject(error, QStringLiteral("静态快照内容不可复用已有身份，编辑后须使用新快照ID"));
    }
    const auto bytes = cost(block);
    if (bytes > 4 * 1024 * 1024) return reject(error, QStringLiteral("静态快照超过缓存预算"));
    const auto id = block.id;
    m_static.insert(id, new ContentBlock(std::move(block)), qMax(1, int((bytes + 1023) / 1024)));
    emit blockChanged(id); return true;
}
void ContentBlockStore::interrupt(const QString &reason, const QString &taskId, TaskState state, BlockSource source)
{
    QStringList changed;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &block = it.value();
        if (block.source != source || (!taskId.isEmpty() && block.taskId != taskId) || (block.state != TaskState::Created && block.state != TaskState::Running)) continue;
        block.state = state;
        // 中断不伪装为业务执行失败，也不删除已收到的内容；版本仍由生产者分配。
        const auto note = (state == TaskState::Interrupted ? (source == BlockSource::Internal ? QStringLiteral("\n内部任务中断，结果可能未知：") : QStringLiteral("\n后台连接中断，任务结果可能未知："))
                          : state == TaskState::Cancelled ? QStringLiteral("\n任务已取消：") : QStringLiteral("\n后台任务未能执行：")) + reason.left(512);
        if (block.text.size() + note.size() <= 512 * 1024 && m_bytes + note.size() * 2 <= 32 * 1024 * 1024) {
            block.text += note; m_bytes += note.size() * 2;
        }
        changed.append(it.key());
    }
    for (const auto &id : changed) emit blockChanged(id);
}
}
