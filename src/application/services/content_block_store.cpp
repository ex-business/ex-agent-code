#include "content_block_store.h"
#include <QJsonDocument>

namespace exagent {
using namespace ipc;
namespace {
qsizetype cost(const ContentBlock &block) { return block.text.size() * 2 + QJsonDocument(block.data).toJson(QJsonDocument::Compact).size(); }
bool reject(QString *error, const QString &reason) { *error = reason; return false; }
}
const ContentBlock *ContentBlockStore::find(const QString &id) const { const auto it = m_blocks.constFind(id); return it == m_blocks.cend() ? nullptr : &it.value(); }
const ContentBlock *ContentBlockStore::task(const QString &taskId) const { for (const auto &block : m_blocks) if (block.taskId == taskId) return &block; return nullptr; }
bool ContentBlockStore::prepare(ContentBlock block, QString *error)
{
    if (!identifier(block.id) || !identifier(block.taskId) || !identifier(block.conversationId)
        || block.title.size() > 200 || block.revision != 0 || block.state != TaskState::Created
        || block.text.size() > 512 * 1024 || m_bytes + cost(block) > 32 * 1024 * 1024)
        return reject(error, QStringLiteral("内容块初始状态、身份或容量无效"));
    if (m_blocks.size() >= 256 || m_blocks.contains(block.id) || task(block.taskId)) return reject(error, QStringLiteral("内容块容量已满或身份重复"));
    m_bytes += cost(block); const auto id = block.id;
    m_blocks.insert(id, std::move(block)); emit blockChanged(id); return true;
}
bool ContentBlockStore::apply(const QString &method, const QJsonObject &event, QString *error)
{
    if ((method != "block.created" && method != "block.patch") || !identifier(event.value("blockId"))
        || !identifier(event.value("taskId")) || !identifier(event.value("conversationId"))
        || !integer(event.value("revision"), 1, 2147483647) || !integer(event.value("taskState"), 0, 4))
        return reject(error, QStringLiteral("内容事件字段无效"));
    const auto id = event.value("blockId").toString();
    const auto *previous = find(id);
    if (!previous || previous->taskId != event.value("taskId").toString() || previous->conversationId != event.value("conversationId").toString())
        return reject(error, QStringLiteral("内容事件超出已授权会话或任务"));
    if (event.value("revision").toInt() != previous->revision + 1) return reject(error, QStringLiteral("内容版本不连续"));
    auto next = *previous;
    const bool created = method == "block.created";
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
    next.state = TaskState(event.value("taskState").toInt()); next.revision = event.value("revision").toInt();
    if (next.state == TaskState::Created && !created) return reject(error, QStringLiteral("运行中的任务不能退回创建状态"));
    const auto nextBytes = m_bytes - cost(*previous) + cost(next);
    if (next.text.size() > 512 * 1024 || nextBytes > 32 * 1024 * 1024) return reject(error, QStringLiteral("内容容量超限，保留当前快照并暂停接收"));
    m_blocks[id] = std::move(next); m_bytes = nextBytes;
    emit blockChanged(id); return true;
}
void ContentBlockStore::interrupt(const QString &reason, const QString &taskId, TaskState state)
{
    QStringList changed;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &block = it.value();
        if ((!taskId.isEmpty() && block.taskId != taskId) || (block.state != TaskState::Created && block.state != TaskState::Running)) continue;
        block.state = state;
        // 中断不伪装为业务执行失败，也不删除已收到的内容；版本仍由生产者分配。
        const auto note = (state == TaskState::Interrupted ? QStringLiteral("\n后台连接中断，任务结果可能未知：")
                          : state == TaskState::Cancelled ? QStringLiteral("\n任务已取消：") : QStringLiteral("\n后台任务未能执行：")) + reason.left(512);
        if (block.text.size() + note.size() <= 512 * 1024 && m_bytes + note.size() * 2 <= 32 * 1024 * 1024) {
            block.text += note; m_bytes += note.size() * 2;
        }
        changed.append(it.key());
    }
    for (const auto &id : changed) emit blockChanged(id);
}
}
