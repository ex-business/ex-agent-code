#pragma once

#include "infrastructure/storage/session_repository.h"
#include "infrastructure/storage/conversation_resource_repository.h"
#include "presentation/chat/message_list_model.h"
#include "presentation/chat/session_list_model.h"
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace exagent {

class ChatController final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ChatController)
    QML_UNCREATABLE("聊天控制器由应用注入")
    Q_PROPERTY(exagent::MessageListModel *messages READ messages CONSTANT)
    Q_PROPERTY(exagent::SessionListModel *sessions READ sessions CONSTANT)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY sessionChanged)
    Q_PROPERTY(QString sessionTitle READ sessionTitle NOTIFY sessionChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(bool previewMode READ previewMode NOTIFY sessionChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QVariantList resourceConversations READ resourceConversations NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantList navigationEntries READ navigationEntries NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantMap navigationInfo READ navigationInfo NOTIFY resourcesChanged)
    Q_PROPERTY(QString resourcePath READ resourcePath CONSTANT)
    Q_PROPERTY(QVariantMap changeSummary READ changeSummary NOTIFY sessionChanged)
    Q_PROPERTY(int activityStatus READ activityStatus NOTIFY sessionChanged)
public:
    explicit ChatController(QObject *parent = nullptr);
    MessageListModel *messages();
    SessionListModel *sessions();
    QString currentSessionId() const;
    QString sessionTitle() const;
    QString draft() const;
    QString errorMessage() const;
    QString storagePath() const;
    bool previewMode() const;
    QVariantList resourceConversations() const;
    QVariantList navigationEntries() const;
    QVariantMap navigationInfo() const;
    QString resourcePath() const;
    QVariantMap changeSummary() const { return m_preview ? m_changeSummary : QVariantMap{}; }
    int activityStatus() const { return m_preview ? m_activityStatus : 0; }
    void setDraft(const QString &text);
    Q_INVOKABLE void newSession();
    Q_INVOKABLE void selectSession(const QString &id);
    Q_INVOKABLE bool sendMessage();
    Q_INVOKABLE void showPreview();
    Q_INVOKABLE void showLongPreview();
    Q_INVOKABLE void showLargeChatPreview();
    Q_INVOKABLE void showConversationPreview();
    Q_INVOKABLE void loadResourceConversation(const QString &id);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setDarkTheme(bool dark);
    void flush();
signals:
    void sessionChanged();
    void draftChanged();
    void errorChanged();
    void resourcesChanged();

private:
    void persistCurrent();
    void refreshSessions();
    void setError(const QString &message);
    QString createId() const;
    MessageListModel m_messages;
    SessionListModel m_sessionModel;
    SessionRepository m_repository;
    ConversationResourceRepository m_resources;
    QList<ChatSession> m_sessions;
    int m_current = -1;
    bool m_preview = false;
    bool m_storageWritable = true;
    QString m_error;
    QString m_previewDraft;
    QString m_previewTitle;
    QVariantMap m_changeSummary;
    int m_activityStatus = 0;
    QTimer m_saveTimer;
};

} // namespace exagent
