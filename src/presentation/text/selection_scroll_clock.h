#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 边缘扩选使用独立时钟，不依赖窗口的动画渲染循环；两次回调按实际时间计算距离。
class SelectionScrollClock : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
    explicit SelectionScrollClock(QObject *parent = nullptr);
    bool running() const { return m_timer.isActive(); }
    void setRunning(bool running);
signals:
    void runningChanged();
    void tick(qreal seconds);
private:
    QTimer m_timer;
    QElapsedTimer m_elapsed;
};
}
