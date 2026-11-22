#include "scroll_diagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QCursor>
#include <algorithm>
#ifdef Q_OS_MACOS
#include "platform/macos/inactive_hover.h"
#endif

ScrollDiagnostics::ScrollDiagnostics(QQuickWindow *window) : QObject(window)
{
    m_clock.start();
    if (window->screen() && window->screen()->refreshRate() > 0) m_budget = 1000.0 / window->screen()->refreshRate();
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/diagnostics";
    QDir().mkpath(directory);
    m_path = directory + QString("/scroll-%1.json").arg(QCoreApplication::applicationPid());
    window->installEventFilter(this);
    connect(window, &QQuickWindow::frameSwapped, this, [this] { presented(); }, Qt::DirectConnection);
    // 按需绘制的帧间隔包含空闲，不能据此判定掉帧；分别记录真实同步与渲染耗时。
    connect(window, &QQuickWindow::beforeSynchronizing, this, [this] {
        std::lock_guard guard(m_mutex);
        m_syncStarted = m_clock.nsecsElapsed() / 1000000.0;
        // GUI在本帧同步阶段阻塞，仅将已进入同步的输入归属到该帧，不能借旧帧提前结算。
        m_syncedInputCount = m_inputCount;
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterSynchronizing, this, [this] {
        const double now = m_clock.nsecsElapsed() / 1000000.0;
        std::lock_guard guard(m_mutex);
        if (m_syncStarted >= 0 && now - m_lastInput < 500 && m_syncDurations.size() < 4000)
            m_syncDurations.push_back(now - m_syncStarted);
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRendering, this, [this] {
        std::lock_guard guard(m_mutex);
        m_renderStarted = m_clock.nsecsElapsed() / 1000000.0;
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, [this] {
        const double now = m_clock.nsecsElapsed() / 1000000.0;
        std::lock_guard guard(m_mutex);
        if (m_renderStarted >= 0 && now - m_lastInput < 500 && m_renderDurations.size() < 4000)
            m_renderDurations.push_back(now - m_renderStarted);
    }, Qt::DirectConnection);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_saveTimer.start(1000);
}
ScrollDiagnostics::~ScrollDiagnostics() { save(); }
bool ScrollDiagnostics::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
        if (event->type() == QEvent::HoverMove && !qobject_cast<QQuickWindow *>(parent())->isActive()) {
            std::lock_guard guard(m_mutex);
            ++m_inactiveHoverCount;
            m_dirty = true;
        }
        // 在 Quick 完成命中和光标切换后采样，保留形状变化，避免焦点恢复后的箭头覆盖证据。
        if (!m_cursorPending) {
            m_cursorPending = true;
            QTimer::singleShot(0, this, [this] {
                m_cursorPending = false;
                const auto *window = qobject_cast<QQuickWindow *>(parent());
                if (!window) return;
                const int shape = int(window->cursor().shape());
                std::lock_guard guard(m_mutex);
                if (m_cursorShapes.size() < 128 && (m_cursorShapes.isEmpty() || m_cursorShapes.last().toObject().value("shape").toInt() != shape)) {
                    m_cursorShapes.append(QJsonObject{{"shape", shape}, {"ms", m_clock.nsecsElapsed() / 1000000.0}});
                    m_dirty = true;
                }
            });
        }
    }
    const bool wheelInput = event->type() == QEvent::Wheel;
    const bool dragInput = event->type() == QEvent::MouseMove && (static_cast<QMouseEvent *>(event)->buttons() & Qt::LeftButton);
    if (!wheelInput && !dragInput) return false;
    const auto *wheel = wheelInput ? static_cast<QWheelEvent *>(event) : nullptr;
    // 触控板开始/结束阶段可只有相位而无位移，不应等待一帧不存在的内容变化。
    if (wheel && wheel->pixelDelta().isNull() && wheel->angleDelta().isNull()) return false;
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    std::lock_guard guard(m_mutex);
    if (now - m_lastInput > 350) m_activeStart = now;
    m_lastInput = now;
    ++m_inputCount;
    const auto *view = parent()->findChild<QQuickItem *>("messageList");
    if (m_inputs.size() < 2000) m_inputs.append(QJsonObject{{"ms", now}, {"kind", wheelInput ? 1 : 2},
        {"pixelY", wheel ? wheel->pixelDelta().y() : 0}, {"angleY", wheel ? wheel->angleDelta().y() : 0},
        {"sampleMs", m_clock.nsecsElapsed() / 1000000.0},
        {"active", qobject_cast<QQuickWindow *>(parent())->isActive()},
        {"viewMoving", view ? view->property("moving").toBool() : false},
        {"viewFlicking", view ? view->property("flicking").toBool() : false},
        {"viewY", view ? view->property("contentY").toDouble() : 0},
        {"originY", view ? view->property("originY").toDouble() : 0},
        {"phase", wheel ? static_cast<int>(wheel->phase()) : 0}});
    m_dirty = true;
    return false;
}
void ScrollDiagnostics::presented()
{
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    std::lock_guard guard(m_mutex);
    if (m_inputCount > 0 && (now - m_lastInput < 350 || m_presentedInput != m_inputCount)) {
        // 排除操作间的空闲等待；保留有输入未展示时的长延迟，不把停顿隐藏掉。
        if (m_lastFrame >= m_activeStart && m_intervals.size() < 10000) m_intervals.push_back(now - m_lastFrame);
        // 合帧时逐笔计算等待时间，不能只取最后一笔而掩盖前面排队的输入。
        for (int index = m_presentedInput; index < m_syncedInputCount && index < m_inputs.size() && m_latency.size() < 2000; ++index) {
            const auto input = m_inputs.at(index).toObject();
            const auto delay = now - input.value("ms").toDouble();
            auto recorded = input;
            recorded.insert("presentedMs", now);
            recorded.insert("inputToFrameMs", delay);
            m_inputs.replace(index, recorded);
            m_latency.push_back(delay);
            (input.value("kind").toInt() == 2 ? m_dragLatency : m_wheelLatency).push_back(delay);
        }
        m_presentedInput = m_syncedInputCount;
        m_dirty = true;
    }
    m_lastFrame = now;
}
void ScrollDiagnostics::save()
{
#ifdef Q_OS_MACOS
    const int systemCursor = macSystemCursorShape();
    {
        std::lock_guard guard(m_mutex);
        if (systemCursor != m_lastSystemCursor) { m_lastSystemCursor = systemCursor; m_dirty = true; }
    }
#endif
    QJsonArray inputs, cursorShapes;
    std::vector<double> intervals, latency, dragLatency, wheelLatency, syncDurations, renderDurations;
    int inputCount, inactiveHoverCount;
    {
        std::lock_guard guard(m_mutex);
        // 停止输入后继续保留短暂读数，用于核对视口是否仍在漂移，不制造动画或新绘制。
        if (!m_dirty && m_clock.nsecsElapsed() / 1000000.0 - m_lastInput > 2000) return;
        m_dirty = false;
        inputs = m_inputs; intervals = m_intervals; latency = m_latency; inputCount = m_inputCount;
        dragLatency = m_dragLatency; wheelLatency = m_wheelLatency;
        syncDurations = m_syncDurations; renderDurations = m_renderDurations;
        cursorShapes = m_cursorShapes;
        inactiveHoverCount = m_inactiveHoverCount;
    }
    const auto statistics = [this](std::vector<double> samples) {
        std::sort(samples.begin(), samples.end());
        const auto percentile = [&samples](double q) { return samples.empty() ? 0.0 : samples[qMin(samples.size() - 1, static_cast<size_t>((samples.size() - 1) * q))]; };
        const int overBudget = std::count_if(samples.begin(), samples.end(), [this](double ms) { return ms > m_budget * 1.5; });
        return QJsonObject{{"samples", static_cast<int>(samples.size())}, {"p50Ms", percentile(0.5)},
            {"p95Ms", percentile(0.95)}, {"maxMs", percentile(1)}, {"overBudgetCount", overBudget}};
    };
    QJsonArray frameIntervals;
    for (double value : intervals) frameIntervals.append(value);
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto *view = parent()->findChild<QQuickItem *>("messageList");
    file.write(QJsonDocument(QJsonObject{{"schemaVersion", 2}, {"renderLoop", qEnvironmentVariable("QSG_RENDER_LOOP", "default")}, {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"sampleMs", m_clock.nsecsElapsed() / 1000000.0},
        {"active", qobject_cast<QQuickWindow *>(parent())->isActive()},
        {"viewMoving", view ? view->property("moving").toBool() : false},
        {"viewFlicking", view ? view->property("flicking").toBool() : false},
        {"viewY", view ? view->property("contentY").toDouble() : 0},
        {"originY", view ? view->property("originY").toDouble() : 0},
        {"frameBudgetMs", m_budget}, {"inputCount", inputCount}, {"frameIntervals", statistics(intervals)},
        {"frameIntervalsIncludeIdle", true}, {"syncDuration", statistics(syncDurations)}, {"renderDuration", statistics(renderDurations)},
        {"cursorShape", qobject_cast<QQuickWindow *>(parent()) ? int(qobject_cast<QQuickWindow *>(parent())->cursor().shape()) : int(Qt::ArrowCursor)},
        {"cursorShapes", cursorShapes},
        {"inactiveHoverCount", inactiveHoverCount},
#ifdef Q_OS_MACOS
        {"systemCursorShape", systemCursor},
#endif
        {"inputToFrame", statistics(latency)}, {"dragInputToFrame", statistics(dragLatency)},
        {"wheelInputToFrame", statistics(wheelLatency)}, {"inputs", inputs}, {"frameIntervalsMs", frameIntervals}}).toJson());
    file.commit();
}
