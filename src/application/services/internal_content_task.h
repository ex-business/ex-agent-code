#pragma once
#include "content_block_adapters.h"
#include <QHash>
#include <QThreadPool>
#include <QStringList>
#include <atomic>
#include <memory>

namespace exagent {
// 内部任务和外部服务共用块归约入口，内部计算不绕行套接字，也不访问GUI对象。
class InternalContentTask final : public QObject {
    Q_OBJECT
public:
    InternalContentTask(ContentBlockStore &store, QObject *parent = nullptr);
    ~InternalContentTask() override;
    bool start(ContentBlock block, QStringList messages, QString sourceTitle, QString *error);
    void cancel(const QString &taskId);
    void stopAll();
signals:
    void failure(QString reason);
private:
    struct Result { QString text; QJsonObject chart; };
    struct Job { QString blockId, chartId; qsizetype bytes; std::shared_ptr<std::atomic_bool> cancel; };
    ContentBlockStore &m_store;
    InternalBlockAdapter m_adapter;
    QThreadPool m_pool;
    QHash<QString, Job> m_jobs;
    qsizetype m_bytes = 0;
};
}
