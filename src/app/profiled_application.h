#pragma once
#include "scroll_diagnostics.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointer>

// 只包围真实窗口的鼠标释放分发，不改变事件处理与业务执行顺序。
class ProfiledApplication final : public QGuiApplication {
public:
    using QGuiApplication::QGuiApplication;
    QPointer<ScrollDiagnostics> diagnostics;
    bool notify(QObject *receiver, QEvent *event) override {
        auto *probe = diagnostics.data();
        if (!probe || receiver != probe->parent() || event->type() != QEvent::MouseButtonRelease
            || static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton)
            return QGuiApplication::notify(receiver, event);
        const double started = probe->timestamp();
        const bool result = QGuiApplication::notify(receiver, event);
        if (diagnostics) diagnostics->recordDispatch(started, diagnostics->timestamp());
        return result;
    }
};
