#pragma once
#include "infrastructure/storage/project_repository.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace exagent {

// 本机历史操作共享同一份已确认快照。持久化成功后才向视图发布新历史。
class ConversationTransfer final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ConversationTransfer)
    QML_UNCREATABLE("由会话宿主提供")
public:
    enum Mode { Send = 2, Fork = 3, Extract = 4, Merge = 5 };
    Q_ENUM(Mode)
    ConversationTransfer(QList<ChatSession> &sessions, ProjectCatalog &catalog, ProjectRepository &repository);
    Q_INVOKABLE QVariantList turns(const QString &sessionId) const;
    Q_INVOKABLE QVariantMap preview(int mode, const QString &sourceId, const QString &targetId, int first, int last);
    Q_INVOKABLE bool commit();
signals:
    void published(QString sessionId);
    void failure(QString reason);
private:
    const ChatSession *find(const QString &id) const;
    QList<ChatSession> &m_sessions;
    ProjectCatalog &m_catalog;
    ProjectRepository &m_repository;
    ChatSession m_source, m_target, m_result;
    bool publish();
    bool m_copying = false;
    bool m_pending = false;
    bool m_create = false;
};
}
