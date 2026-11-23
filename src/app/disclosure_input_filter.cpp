#include "disclosure_input_filter.h"
#include <QEvent>
#include <QMouseEvent>
#include <QQuickWindow>

DisclosureInputFilter::DisclosureInputFilter(QQuickWindow *window, QObject *coordinator)
    : QObject(window), m_coordinator(coordinator)
{
    window->installEventFilter(this);
}
bool DisclosureInputFilter::eventFilter(QObject *, QEvent *event)
{
    if (m_coordinator && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        // 先让Quick完成按下命中，再结束旧动画，避免提前改布局影响本次命中对象。
        QMetaObject::invokeMethod(m_coordinator, "interruptForInput", Qt::QueuedConnection);
    }
    return false;
}
