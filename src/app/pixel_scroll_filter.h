#pragma once
#include <QQuickItem>
#include <QPointer>
#include <QVector>
class QQuickItem;
class QQuickWindow;

// 兼容报告 pixelDelta、但缺失手势 phase 的鼠标驱动，避免退化为整格滚动。
class PixelScrollFilter final : public QQuickItem {
public:
    explicit PixelScrollFilter(QQuickWindow *window);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void updatePolish() override;
private:
    struct Pending { QPointer<QQuickItem> item; qreal x; qreal y; };
    void apply();
    QQuickWindow *m_window;
    QVector<Pending> m_pending;
};
