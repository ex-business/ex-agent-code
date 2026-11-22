#pragma once
#include "presentation/chat/message_list_model.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 选区依附消息身份而非委托对象，列表回收和历史前插不会丢失已选原文。
class ChatSelectionDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MessageListModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int selectionLength READ selectionLength NOTIFY selectionChanged)
public:
    explicit ChatSelectionDocument(QObject *parent = nullptr) : QObject(parent) {}
    MessageListModel *model() const { return m_model; }
    void setModel(MessageListModel *model);
    int selectionLength() const;
    Q_INVOKABLE int orderOf(const QString &id) const { return m_rows.value(id, -1); }
    Q_INVOKABLE void select(const QString &anchorId, int anchorBlock, int anchorOffset,
                            const QString &cursorId, int cursorBlock, int cursorOffset);
    Q_INVOKABLE QVariantMap anchorPosition() const;
    Q_INVOKABLE QVariantMap range(const QString &id, int block, int length) const;
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariant selectedContent() const;
    Q_INVOKABLE void selectLoaded();
signals:
    void modelChanged();
    void selectionChanged();
private:
    struct Position { QString id; int block = 0; int offset = 0; QString blockKey; };
    bool less(const Position &a, const Position &b) const;
    void rebuildRows();
    void reconcilePositions();
    QPointer<MessageListModel> m_model;
    QHash<QString, int> m_rows;
    Position m_anchor, m_cursor;
};
}
