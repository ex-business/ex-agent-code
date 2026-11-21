#pragma once

#include "domain/chat/chat_message.h"
#include <QAbstractListModel>
#include <QCache>
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace exagent {

class MessageListModel final : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(MessageListModel)
    QML_UNCREATABLE("消息由聊天控制器管理")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY countChanged)

public:
    enum Kind { User = 1, Assistant = 2, Tool = 3, Notice = 4 };
    Q_ENUM(Kind)
    enum Roles { IdRole = Qt::UserRole + 1, KindRole, ContentRole, TitleRole,
                 StatusRole, ExpandedRole, BlocksRole, TimeRole, FilesRole, ActivityRole, AttachmentsRole };
    explicit MessageListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    int totalCount() const;
    bool hasOlder() const;
    Q_INVOKABLE int loadOlder();
    const QList<ChatMessage> &messages() const;
    void replace(QList<ChatMessage> messages);
    void replacePaged(QList<ChatMessage> latest, int total, std::function<QList<ChatMessage>()> olderLoader);
    void append(ChatMessage message);
    void appendText(const QString &messageId, const QString &delta);
    Q_INVOKABLE void toggleExpanded(int row);
    void setDark(bool dark);
    QStringList selectionBlocks(int row) const;

signals:
    void countChanged();
    void contentUpdated();
    void presentationChanged();

private:
    bool m_dark = false;
    QList<ChatMessage> m_messages;
    int m_firstLoaded = 0;
    int m_pagedTotal = -1;
    std::function<QList<ChatMessage>()> m_olderLoader;
    mutable QCache<QString, QVariantList> m_blocksCache{4096};
};

} // namespace exagent
