#include "pixel_scroll_filter.h"
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtMath>
#include <utility>

PixelScrollFilter::PixelScrollFilter(QQuickWindow *window) : QQuickItem(window->contentItem()), m_window(window)
{
    window->installEventFilter(this);
    setAcceptedMouseButtons(Qt::NoButton);
}
bool PixelScrollFilter::eventFilter(QObject *, QEvent *event)
{
    if (event->type() != QEvent::Wheel) return false;
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (wheel->phase() != Qt::NoScrollPhase || wheel->pixelDelta().isNull()) return false;
    auto *item = m_window->contentItem();
    QPointF point = wheel->position();
    // 使用实际命中层级选择最内层滚动视口，菜单与未声明支持的控件保留原生行为。
    for (int depth = 0; depth < 64; ++depth) {
        auto *child = item->childAt(point.x(), point.y());
        if (!child) break;
        point = child->mapFromItem(item, point);
        item = child;
    }
    qreal remainingX = wheel->pixelDelta().x();
    qreal remainingY = wheel->pixelDelta().y();
    bool consumed = false;
    for (; item; item = item->parentItem()) {
        if (!item->property("pixelWheelEnabled").toBool()) continue;
        int pendingIndex = -1;
        for (int index = 0; index < m_pending.size(); ++index) if (m_pending[index].item == item) { pendingIndex = index; break; }
        const qreal currentX = pendingIndex >= 0 ? m_pending[pendingIndex].x : item->property("contentX").toReal();
        const qreal currentY = pendingIndex >= 0 ? m_pending[pendingIndex].y : item->property("contentY").toReal();
        const qreal minX = item->property("originX").toReal() - item->property("leftMargin").toReal();
        const qreal minY = item->property("originY").toReal() - item->property("topMargin").toReal();
        const qreal maxX = minX + qMax(0.0, item->property("contentWidth").toReal() - item->width() + item->property("leftMargin").toReal() + item->property("rightMargin").toReal());
        const qreal maxY = minY + qMax(0.0, item->property("contentHeight").toReal() - item->height() + item->property("topMargin").toReal() + item->property("bottomMargin").toReal());
        const qreal nextX = qBound(minX, currentX - remainingX, maxX);
        const qreal nextY = qBound(minY, currentY - remainingY, maxY);
        const qreal usedX = currentX - nextX;
        const qreal usedY = currentY - nextY;
        if (!qFuzzyIsNull(usedX) || !qFuzzyIsNull(usedY)) {
            if (item->property("flicking").toBool()) QMetaObject::invokeMethod(item, "cancelFlick");
            if (pendingIndex < 0) m_pending.append({item, nextX, nextY});
            else { m_pending[pendingIndex].x = nextX; m_pending[pendingIndex].y = nextY; }
            QMetaObject::invokeMethod(item, "pixelScrollStarted");
            consumed = true;
            remainingX -= usedX;
            remainingY -= usedY;
        }
        if (qFuzzyIsNull(remainingX) && qFuzzyIsNull(remainingY)) break;
    }
    if (!consumed) return false;
    // 同一帧内多个输入合并一次位置更新，保留全部位移，不叠加人工惯性。
    polish();
    wheel->accept();
    return true;
}
void PixelScrollFilter::apply()
{
    const auto pending = std::exchange(m_pending, {});
    for (const auto &entry : pending) {
        if (!entry.item) continue;
        entry.item->setProperty("pixelAligned", false);
        entry.item->setProperty("contentX", entry.x);
        entry.item->setProperty("contentY", entry.y);
    }
}
void PixelScrollFilter::updatePolish() { apply(); }
