#include "internal_content_task.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QUuid>
#include <QtConcurrentRun>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(internalContentLog, "exagent.content.internal")

namespace exagent {
using namespace content;
InternalContentTask::InternalContentTask(ContentBlockStore &store, QObject *parent)
    : QObject(parent), m_store(store), m_adapter(store)
{
    m_pool.setMaxThreadCount(2); m_pool.setThreadPriority(QThread::LowPriority);
}
InternalContentTask::~InternalContentTask()
{
    // 先取消快照计算再等待线程池退出，完成回调随QObject销毁，不访问已销毁控制器。
    for (const auto &job : m_jobs) job.cancel->store(true);
}
bool InternalContentTask::start(ContentBlock block, QStringList messages, QString sourceTitle, QString *error)
{
    qsizetype bytes = 0;
    for (const auto &message : messages) bytes += message.size() * 2;
    if (m_jobs.size() >= 4 || bytes > 32 * 1024 * 1024 || m_bytes + bytes > 32 * 1024 * 1024) {
        if (error) *error = QStringLiteral("内部任务或内容快照超过容量，请减少已加载内容后重试"); return false;
    }
    const auto taskId = block.taskId, blockId = block.id;
    auto chart = block;
    chart.id = QUuid::createUuid().toString(QUuid::WithoutBraces); chart.kind = BlockKind::Chart; chart.title = QStringLiteral("消息长度分布");
    const auto chartId = chart.id;
    if (!m_adapter.prepare(std::move(block), error)) return false;
    if (!m_adapter.prepare(std::move(chart), error)) {
        m_store.interrupt(QStringLiteral("统计图表无法登记"), taskId, TaskState::Failed, BlockSource::Internal); return false;
    }
    if (!m_adapter.create(blockId, QStringLiteral("正在统计已加载消息。\n"), error)
        || !m_adapter.create(chartId, QStringLiteral("正在计算消息长度分布。"), error)) {
        m_store.interrupt(QStringLiteral("内部任务无法建立初始快照"), taskId, TaskState::Failed, BlockSource::Internal); return false;
    }
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_jobs.insert(taskId, {blockId, chartId, bytes, cancel}); m_bytes += bytes;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, taskId, cancel] {
        const auto job = m_jobs.take(taskId); m_bytes -= job.bytes;
        if (!cancel->load()) {
            QString error;
            try {
                const auto result = watcher->result();
                if (!m_adapter.append(job.blockId, result.text, TaskState::Complete, &error)
                    || !m_adapter.append(job.chartId, QStringLiteral("\n消息长度分布已完成。"), TaskState::Complete, &error, result.chart)) {
                    m_store.interrupt(error, taskId, TaskState::Failed, BlockSource::Internal); emit failure(error);
                }
            } catch (...) {
                error = QStringLiteral("内部内容计算失败，已有快照保留");
                m_store.interrupt(error, taskId, TaskState::Failed, BlockSource::Internal); emit failure(error);
            }
            if (!error.isEmpty()) qCWarning(internalContentLog).noquote() << "内部任务结束异常，taskId：" << taskId << "原因：" << error;
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [messages = std::move(messages), sourceTitle = std::move(sourceTitle), cancel] {
        qint64 units = 0, scalars = 0, newlines = 0;
        QJsonArray values; int ordinal = 0;
        for (const auto &message : messages) {
            units += message.size();
            if (ordinal < 64) values.append(QJsonObject{{"label", QString::number(ordinal + 1)}, {"value", double(message.size())}});
            ++ordinal;
            for (qsizetype i = 0; i < message.size(); ++i) {
                if ((i & 4095) == 0 && cancel->load()) return Result{};
                if (message[i] == '\n') ++newlines;
                if (message[i].isHighSurrogate() && i + 1 < message.size() && message[i + 1].isLowSurrogate()) ++i;
                ++scalars;
            }
        }
        Result result;
        result.chart = {{"series", values}, {"unit", QStringLiteral("UTF-16单元")}, {"totalMessages", messages.size()}};
        result.text = QStringLiteral("来源：%1\n仅统计已加载的 %2 条消息，不读取尚未加载的历史。\nUnicode码点：%3\nUTF-16代码单元：%4\n换行符（LF）：%5\n统计已完成。\n")
            .arg(sourceTitle.left(200)).arg(messages.size()).arg(scalars).arg(units).arg(newlines);
        return result;
    }));
    return true;
}
void InternalContentTask::cancel(const QString &taskId)
{
    const auto it = m_jobs.constFind(taskId);
    if (it == m_jobs.cend() || it->cancel->exchange(true)) return;
    QString error;
    for (const auto &id : m_store.taskBlocks(taskId)) {
        const auto *block = m_store.find(id);
        if (block && (block->state == TaskState::Created || block->state == TaskState::Running))
            if (!m_adapter.append(id, QStringLiteral("内部任务已取消，已有内容保留。\n"), TaskState::Cancelled, &error)) emit failure(error);
    }
}
void InternalContentTask::stopAll()
{
    for (const auto &job : m_jobs) job.cancel->store(true);
    m_store.interrupt(QStringLiteral("应用正在退出，内部任务不自动重新执行"), {}, TaskState::Interrupted, BlockSource::Internal);
}
}
