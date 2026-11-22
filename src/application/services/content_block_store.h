#pragma once
#include "domain/content/content_block.h"
#include <QCache>
#include <QObject>
#include <QHash>
#include <QSet>

namespace exagent {
using ContentBlock = content::ContentBlock;
// 内容状态与视图解耦；每个后台事件只能更新宿主明确授权的任务/块绑定。
class ContentBlockStore final : public QObject {
    Q_OBJECT
public:
    explicit ContentBlockStore(QObject *parent = nullptr) : QObject(parent) {}
    bool prepare(ContentBlock block, QString *error);
    bool applyEvent(const content::BlockEvent &event, QString *error);
    bool apply(const QString &method, const QJsonObject &event, QString *error);
    void interrupt(const QString &reason, const QString &taskId = {}, content::TaskState state = content::TaskState::Interrupted, content::BlockSource source = content::BlockSource::External);
    const ContentBlock *find(const QString &id) const;
    const ContentBlock *task(const QString &taskId) const;
    QStringList taskBlocks(const QString &taskId) const;
    int taskState(const QString &taskId) const;
    QVariantMap snapshot(const QString &blockId) const;
signals:
    void blockChanged(QString id);
private:
    bool applySnapshot(const content::BlockEvent &event, QString *error);
    QHash<QString, ContentBlock> m_blocks;
    QHash<QString, QStringList> m_tasks;
    // 静态快照可随时从资源重建，使用独立LRU预算，不挤占动态任务保留容量。
    mutable QCache<QString, ContentBlock> m_static{4096};
    qsizetype m_bytes = 0;
};
}
