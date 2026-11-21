#pragma once

#include "domain/chat/chat_message.h"
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace exagent {

class SessionListModel final : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(SessionListModel)
    QML_UNCREATABLE("会话由聊天控制器管理")
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, PreviewRole, DateRole };
    explicit SessionListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(const QList<ChatSession> &sessions);
private:
    struct Summary { QString id; QString title; QString preview; QString date; };
    QList<Summary> m_items;
};

} // namespace exagent
