#pragma once
#include "domain/ipc/protocol.h"
#include <QObject>
#include <QHash>

namespace exagent {
struct ContentBlock {
    QString id, taskId, conversationId, title, text;
    ipc::BlockKind kind = ipc::BlockKind::Text;
    ipc::TaskState state = ipc::TaskState::Created;
    int revision = 0;
    QJsonObject data;
};
// 内容状态与视图解耦；每个后台事件只能更新宿主明确授权的任务/块绑定。
class ContentBlockStore final : public QObject {
    Q_OBJECT
public:
    explicit ContentBlockStore(QObject *parent = nullptr) : QObject(parent) {}
    bool prepare(ContentBlock block, QString *error);
    bool apply(const QString &method, const QJsonObject &event, QString *error);
    void interrupt(const QString &reason, const QString &taskId = {}, ipc::TaskState state = ipc::TaskState::Interrupted);
    const ContentBlock *find(const QString &id) const;
    const ContentBlock *task(const QString &taskId) const;
signals:
    void blockChanged(QString id);
private:
    QHash<QString, ContentBlock> m_blocks;
    qsizetype m_bytes = 0;
};
}
