#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 在窗口层观察完整鼠标序列，不抢占控件事件；起点委托回收后仍能完成跨消息拖选。
class ChatSelectionTracker : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool tracking MEMBER m_tracking)
public:
    explicit ChatSelectionTracker(QQuickItem *parent = nullptr);
    ~ChatSelectionTracker() override;
signals:
    void dragMoved(QPointF point);
    void dragReleased(QPointF point);
    void dragCancelled();
protected:
    bool eventFilter(QObject *, QEvent *event) override;
private:
    bool m_tracking = false;
    QPointer<QQuickWindow> m_window;
};
}
