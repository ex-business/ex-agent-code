#include "pixel_scroll_filter.h"
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtMath>

PixelScrollFilter::PixelScrollFilter(QQuickWindow *window) : QObject(window), m_window(window)
{
    window->installEventFilter(this);
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
        const qreal currentX = item->property("contentX").toReal();
        const qreal currentY = item->property("contentY").toReal();
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
            QMetaObject::invokeMethod(item, "pixelScrollStarted");
            // 在输入阶段更新视口，让布局在本帧 polish 前完成；场景图自行合并绘制。
            // 不在 updatePolish 内再次移动 ListView，避免布局再排到下一帧。
            item->setProperty("pixelAligned", false);
            if (!qFuzzyIsNull(usedX)) item->setProperty("contentX", nextX);
            if (!qFuzzyIsNull(usedY)) item->setProperty("contentY", nextY);
            consumed = true;
            remainingX -= usedX;
            remainingY -= usedY;
        }
        if (qFuzzyIsNull(remainingX) && qFuzzyIsNull(remainingY)) break;
    }
    if (!consumed) return false;
    wheel->accept();
    return true;
}
