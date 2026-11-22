#pragma once
#include "content_block_adapters.h"
#include "media_catalog.h"

namespace exagent {
class MediaContentTask final : public QObject {
    Q_OBJECT
public:
    MediaContentTask(ContentBlockStore &store, MediaCatalog &catalog, QObject *parent = nullptr);
    ~MediaContentTask() override;
    bool start(ContentBlock block, QString folder, QString *error);
    void cancel(const QString &taskId);
    void stopAll();
signals:
    void failure(QString reason);
private:
    struct Job { QString imageId, sequenceId; std::shared_ptr<std::atomic_bool> cancel; };
    ContentBlockStore &m_store;
    MediaCatalog &m_catalog;
    InternalBlockAdapter m_adapter;
    QThreadPool m_pool;
    QHash<QString, Job> m_jobs;
};
}
