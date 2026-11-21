#include "chat_selection_tracker.h"
#include <QMouseEvent>

namespace exagent {
ChatSelectionTracker::ChatSelectionTracker(QQuickItem *parent) : QQuickItem(parent)
{
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        if (m_window) m_window->removeEventFilter(this);
        m_window = window;
        if (window) window->installEventFilter(this);
    });
}
ChatSelectionTracker::~ChatSelectionTracker() { if (m_window) m_window->removeEventFilter(this); }
bool ChatSelectionTracker::eventFilter(QObject *, QEvent *event)
{
    if (!m_tracking) return false;
    if (event->type() == QEvent::MouseMove) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->buttons() & Qt::LeftButton) emit dragMoved(mouse->position());
    } else if (event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) emit dragReleased(mouse->position());
    } else if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) {
        emit dragCancelled();
    }
    return false;
}
}
