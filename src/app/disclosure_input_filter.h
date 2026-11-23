#pragma once
#include <QObject>
#include <QPointer>
class QQuickWindow;

// 只通知动效协调器，不消费或合成输入，不修改点击、拖选与双击的分发。
class DisclosureInputFilter final : public QObject {
public:
    DisclosureInputFilter(QQuickWindow *window, QObject *coordinator);
protected:
    bool eventFilter(QObject *, QEvent *event) override;
private:
    QPointer<QObject> m_coordinator;
};
