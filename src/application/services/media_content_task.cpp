#include "media_content_task.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QUuid>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(mediaTaskLog, "exagent.content.media")

namespace exagent {
using namespace content;
MediaContentTask::MediaContentTask(ContentBlockStore &store, MediaCatalog &catalog, QObject *parent)
    : QObject(parent), m_store(store), m_catalog(catalog), m_adapter(store) { m_pool.setMaxThreadCount(2); }
MediaContentTask::~MediaContentTask() { for (const auto &job : m_jobs) job.cancel->store(true); }
bool MediaContentTask::start(ContentBlock block, QString folder, QString *error)
{
    if (m_jobs.size() >= 2) { *error = QStringLiteral("图片目录加载任务已满，请稍后重试"); return false; }
    const auto taskId = block.taskId, sequenceId = block.id;
    block.kind = BlockKind::ImageSequence; block.title = QStringLiteral("图片序列");
    auto image = block; image.id = QUuid::createUuid().toString(QUuid::WithoutBraces); image.kind = BlockKind::Image; image.title = QStringLiteral("首帧预览");
    const auto imageId = image.id;
    if (!m_adapter.prepare(std::move(block), error)) return false;
    if (!m_adapter.prepare(std::move(image), error)) { m_store.interrupt(*error, taskId, TaskState::Failed, BlockSource::Internal); return false; }
    if (!m_adapter.create(sequenceId, QStringLiteral("正在读取图片目录。"), error) || !m_adapter.create(imageId, QStringLiteral("正在准备首帧。"), error)) {
        m_store.interrupt(*error, taskId, TaskState::Failed, BlockSource::Internal); return false;
    }
    auto cancel = std::make_shared<std::atomic_bool>(false); m_jobs.insert(taskId, {imageId, sequenceId, cancel});
    auto *watcher = new QFutureWatcher<MediaImport>(this);
    connect(watcher, &QFutureWatcher<MediaImport>::finished, this, [this, watcher, taskId, cancel] {
        const auto job = m_jobs.take(taskId);
        if (!cancel->load()) {
            QString error;
            try {
                const auto result = watcher->result(); error = result.error;
                if (error.isEmpty()) {
                    const auto refs = m_catalog.registerFiles(result.files, &error);
                    if (error.isEmpty()) {
                        if (!m_adapter.append(job.sequenceId, QStringLiteral("\n已加载%1帧，按文件名自然顺序排列。").arg(refs.size()), TaskState::Complete, &error,
                            {{"frames", refs}, {"durationMs", 100}})
                            || !m_adapter.append(job.imageId, QStringLiteral("\n首帧已就绪。"), TaskState::Complete, &error, refs.first().toObject())) {
                            if (error.isEmpty()) error = QStringLiteral("媒体内容无法归约，已有快照保留");
                        } else qCInfo(mediaTaskLog) << "图片任务已完成，taskId：" << taskId << "帧数：" << refs.size();
                    }
                }
            } catch (...) { error = QStringLiteral("图片目录处理失败，原始图片未修改"); }
            if (!error.isEmpty()) { qCWarning(mediaTaskLog).noquote() << "图片任务失败，taskId：" << taskId << "原因：" << error; m_store.interrupt(error, taskId, TaskState::Failed, BlockSource::Internal); emit failure(error); }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [folder = std::move(folder), cancel] { return MediaCatalog::inspectFolder(folder, cancel); }));
    return true;
}
void MediaContentTask::cancel(const QString &taskId)
{
    const auto it = m_jobs.constFind(taskId); if (it == m_jobs.cend()) return;
    it->cancel->store(true); m_store.interrupt(QStringLiteral("图片目录加载已取消"), taskId, TaskState::Cancelled, BlockSource::Internal);
}
void MediaContentTask::stopAll()
{
    for (const auto &job : m_jobs) job.cancel->store(true);
    // 只中断本服务拥有的任务，不触碰其他内部生产者。
    for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
        m_store.interrupt(QStringLiteral("应用退出，图片目录加载已中断"), it.key(), TaskState::Interrupted, BlockSource::Internal);
}
}
