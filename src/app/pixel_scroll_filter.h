#pragma once
#include <QObject>
class QQuickItem;
class QQuickWindow;

// 兼容报告 pixelDelta、但缺失手势 phase 的鼠标驱动，避免退化为整格滚动。
class PixelScrollFilter final : public QObject {
public:
    explicit PixelScrollFilter(QQuickWindow *window);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QQuickWindow *m_window;
};
