#pragma once

#include "infrastructure/storage/session_repository.h"
#include "infrastructure/storage/conversation_resource_repository.h"
#include "presentation/chat/message_list_model.h"
#include "presentation/chat/session_list_model.h"
#include <QObject>
#include "application/services/local_service_process.h"
#include "application/services/content_block_store.h"
#include "application/services/internal_content_task.h"
#include <QTimer>
#include "application/services/media_content_task.h"
#include "presentation/blocks/block_view_registry.h"
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
    Q_PROPERTY(int serviceState READ serviceState NOTIFY serviceStateChanged)
    Q_PROPERTY(int backgroundTaskState READ backgroundTaskState NOTIFY backgroundTaskChanged)
    Q_PROPERTY(QString backgroundTaskId READ backgroundTaskId NOTIFY backgroundTaskChanged)
    Q_PROPERTY(int internalTaskState READ internalTaskState NOTIFY backgroundTaskChanged)
    Q_PROPERTY(exagent::BlockViewRegistry *blockViews READ blockViews CONSTANT)
    Q_PROPERTY(int activityStatus READ activityStatus NOTIFY sessionChanged)
public:
    explicit ChatController(QObject *parent = nullptr);
    ~ChatController() override;
    BlockViewRegistry *blockViews() { return &m_blockViews; }
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
    int serviceState() const { return int(m_service.state()); }
    int backgroundTaskState() const;
    QString backgroundTaskId() const { return m_preview ? QString{} : m_backgroundTasks.value(currentSessionId()); }
    Q_INVOKABLE void runBackgroundTask();
    Q_INVOKABLE void cancelBackgroundTask();
    int internalTaskState() const;
    Q_INVOKABLE void analyzeLoadedConversation();
    Q_INVOKABLE void cancelInternalTask();
    Q_INVOKABLE void requestBlockExport(const QVariantMap &block) { emit blockExportRequested(block); }
    Q_INVOKABLE void exportBlock(const QVariantMap &block, const QUrl &target);
    Q_INVOKABLE void openImageSequence(const QUrl &folder);
    Q_INVOKABLE void saveMedia(const QVariantMap &block, const QString &resourceId, const QUrl &attachment, const QUrl &target);
    Q_INVOKABLE QUrl mediaSource(const QVariantMap &block, const QString &resourceId) const;
signals:
    void mediaSaved(QString path);
    void blockExportRequested(QVariantMap block);
    void blockExported(QString path);
    void serviceStateChanged();
    void backgroundTaskChanged();
    void sessionChanged();
    void draftChanged();
    void errorChanged();
    void resourcesChanged();

private:
    void drainServiceBlocks();
    void applyServiceBlock(const QString &id);
    void dispatchBackgroundTasks();
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
    QTimer m_blockUpdateTimer;
    QList<QString> m_dirtyBlocks;
    LocalServiceProcess m_service;
    ContentBlockStore m_blocks;
    BlockViewRegistry m_blockViews;
    ExternalBlockAdapter m_externalBlocks;
    InternalContentTask m_internalTask;
    MediaCatalog m_mediaCatalog;
    MediaContentTask m_mediaTask;
    QThreadPool m_exporter;
    int m_exportPending = 0;
    QHash<QString, QString> m_internalTasks;
    QHash<QString, QString> m_backgroundTasks;
    QList<QJsonObject> m_waitingTasks;
    QHash<QString, QString> m_taskRequests;
};

} // namespace exagent
