#pragma once
#include <QGuiApplication>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class NavigationGuard : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE bool blocked(QObject *source = nullptr) const
    {
        // 每次导航都核对当前模态框。系统回调和快捷键也不能把编辑中的对话框带到其他会话。
        if (QGuiApplication::modalWindow()) return true;
        for (auto *window : QGuiApplication::topLevelWindows()) {
            if (!window->isVisible()) continue;
            if (hasDialog(window, source)) return true;
        }
        return false;
    }
private:
    static bool hasDialog(QObject *parent, QObject *source)
    {
        for (auto *child : parent->children()) {
            if (child != source && child->property("visible").toBool()) {
                const auto modal = child->property("modal");
                if ((modal.metaType().id() == QMetaType::Bool && modal.toBool()) ||
                    child->property("modality").toInt() != Qt::NonModal) return true;
            }
            if (hasDialog(child,source)) return true;
        }
        return false;
    }
};
}
