#include "selection_scroll_clock.h"

namespace exagent {
SelectionScrollClock::SelectionScrollClock(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // GUI 短暂繁忙后不补跳大段内容，避免越过用户正在选择的文字。
        const qreal seconds = qBound(0.001, m_elapsed.nsecsElapsed() / 1e9, 0.05);
        m_elapsed.restart();
        emit tick(seconds);
    });
}
void SelectionScrollClock::setRunning(bool running)
{
    if (running == m_timer.isActive()) return;
    if (running) {
        m_elapsed.start();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit runningChanged();
}
}
