#pragma once
#include "application/services/content_block_store.h"
#include <QtQml/qqmlregistration.h>
#include "block_window_model.h"

namespace exagent {
class BlockViewRegistry final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(BlockViewRegistry)
    QML_UNCREATABLE("块视图由宿主管理")
    Q_PROPERTY(exagent::BlockWindowModel *windowModel READ windowModel CONSTANT)
    Q_PROPERTY(QVariantList detachedBlocks READ detachedBlocks NOTIFY viewsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    enum Placement { Inline = 1, Windowed = 2 };
    Q_ENUM(Placement)
    enum Expansion { Expanded = 1, Collapsed = 2 };
    Q_ENUM(Expansion)
    enum Visibility { Visible = 1, Hidden = 2 };
    Q_ENUM(Visibility)
    explicit BlockViewRegistry(ContentBlockStore &store, QObject *parent = nullptr);
    BlockWindowModel *windowModel() { return &m_windows; }
    int revision() const { return m_revision; }
    QVariantList detachedBlocks() const;
    Q_INVOKABLE QVariantMap state(const QString &id) const;
    Q_INVOKABLE QVariantMap snapshot(const QString &id) const;
    Q_INVOKABLE void detach(const QVariantMap &block);
    Q_INVOKABLE void reattach(const QString &id);
    Q_INVOKABLE void setExpanded(const QString &id, int expansion);
    Q_INVOKABLE void closeInline(const QString &id);
    Q_INVOKABLE void reopen(const QString &id);
    Q_INVOKABLE QVariantMap presentation(const QString &id) const;
    Q_INVOKABLE void savePresentation(const QString &id, const QVariantMap &value);
signals:
    void focusRequested(QString blockId);
    void viewsChanged();
    void changed();
    void failure(QString reason);
private:
    struct View { int placement = 1, expansion = 1, visibility = 1; QString viewId; QVariantMap block, presentation; };
    View *ensure(const QString &id);
    void touch();
    ContentBlockStore &m_store;
    QHash<QString, View> m_views;
    BlockWindowModel m_windows;
    int m_revision = 0;
};
}
