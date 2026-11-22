#pragma once

#include "domain/chat/chat_message.h"
#include "activity_group_projection.h"
#include <QAbstractListModel>
#include <QCache>
#include <QPointer>
#include <atomic>
#include <memory>
#include "application/services/content_block_adapters.h"
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace exagent {

class MessageListModel final : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(MessageListModel)
    QML_UNCREATABLE("消息由聊天控制器管理")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool olderLoading READ olderLoading NOTIFY paginationChanged)
    Q_PROPERTY(bool olderReady READ olderReady NOTIFY paginationChanged)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY countChanged)

public:
    enum Kind { User = 1, Assistant = 2, Tool = 3, Notice = 4 };
    Q_ENUM(Kind)
    enum Roles { IdRole = Qt::UserRole + 1, KindRole, ContentRole, TitleRole,
                 StatusRole, ExpandedRole, BlocksRole, TimeRole, FilesRole, ActivityRole, AttachmentsRole, GroupRole };
    explicit MessageListModel(QObject *parent = nullptr);
    ~MessageListModel() override { if (m_pageCancel) m_pageCancel->store(true); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    int totalCount() const;
    bool hasOlder() const;
    Q_INVOKABLE int loadOlder();
    Q_INVOKABLE void prefetchOlder();
    bool olderLoading() const { return m_loadingPage; }
    bool olderReady() const { return m_pagedTotal < 0 || !m_pageBuffer.isEmpty(); }
    struct OlderPage { QList<ChatMessage> messages; QString error; };
    const QList<ChatMessage> &messages() const;
    void setContentContext(const QString &conversationId, ContentBlockStore *store);
    void replace(QList<ChatMessage> messages);
    void replacePaged(QList<ChatMessage> latest, int total, int nextPage, std::function<OlderPage(int)> olderLoader);
    void append(ChatMessage message);
    void upsertServiceMessage(ChatMessage message);
    void appendText(const QString &messageId, const QString &delta);
    Q_INVOKABLE void toggleExpanded(int row);
    Q_INVOKABLE void toggleActivityGroup(int row);
    void setDark(bool dark);
    QStringList selectionBlocks(int row) const;
    QStringList selectionBlockKeys(int row) const;

signals:
    void countChanged();
    void contentUpdated();
    void presentationChanged();
    void paginationChanged();
    void olderPageReady();
    void pageFailure(QString error);

private:
    void refreshGroups();
    ActivityGroupProjection m_groups;
    ContentBlockStore m_fallbackBlocks;
    QPointer<ContentBlockStore> m_contentStore;
    QString m_conversationId = QStringLiteral("messages");
    bool m_dark = false;
    QList<ChatMessage> m_messages;
    int m_firstLoaded = 0;
    int m_pagedTotal = -1;
    std::function<OlderPage(int)> m_olderLoader;
    QList<ChatMessage> m_pageBuffer;
    int m_nextPage = -1, m_activeReaders = 0;
    bool m_loadingPage = false, m_pageFailed = false;
    quint64 m_pageGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_pageCancel;
    mutable QCache<QString, QVariantList> m_blocksCache{4096};
};

} // namespace exagent
