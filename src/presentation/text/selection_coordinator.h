#pragma once
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
namespace exagent {
// 每个窗口引擎只有一个选区拥有者和复制入口，虚拟控件回收后不残留快捷键竞争。
class SelectionCoordinator final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SelectionCoordinator)
    QML_SINGLETON
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
public:
    explicit SelectionCoordinator(QObject *parent = nullptr) : QObject(parent) {}
    bool hasSelection() const;
    Q_INVOKABLE void activate(QObject *owner);
    Q_INVOKABLE void changed(QObject *owner);
    Q_INVOKABLE void copy();
signals:
    void selectionChanged();
private:
    QPointer<QObject> m_owner;
    QMetaObject::Connection m_destroyed;
};
}
