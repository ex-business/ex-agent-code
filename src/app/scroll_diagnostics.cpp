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
#include <QInputDevice>
#include <QMouseEvent>
#include <QCursor>
#include <algorithm>
#include <QLoggingCategory>
#include <cstdio>
#ifdef Q_OS_MACOS
#include "platform/macos/inactive_hover.h"
#include "platform/macos/drawable_timing.h"
#endif

namespace {
std::mutex timingHandlerMutex;
ScrollDiagnostics *timingTarget = nullptr;
QtMessageHandler previousHandler = nullptr;
void timingHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (context.category && QByteArray(context.category) == "qt.scenegraph.time.renderloop") {
        std::lock_guard guard(timingHandlerMutex);
        if (timingTarget) { timingTarget->recordQtTiming(message); return; }
    }
    if (previousHandler) previousHandler(type, context, message);
    else std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
}
}

ScrollDiagnostics::ScrollDiagnostics(QQuickWindow *window) : QObject(window)
{
    // 主聊天列表在根窗口加载完成后已创建；同步回调只读取弱引用，避免逐帧遍历对象树。
    m_view = window->findChild<QQuickItem *>("messageList");
    m_clock.start();
    m_disclosureMode = QCoreApplication::arguments().contains("--profile-disclosure");
    m_verboseTiming = m_disclosureMode && !QCoreApplication::arguments().contains("--light-frame-trace");
    m_profileCoverage = QCoreApplication::arguments().contains("--profile-coverage");
    if (window->screen() && window->screen()->refreshRate() > 0) m_budget = 1000.0 / window->screen()->refreshRate();
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/diagnostics";
    QDir().mkpath(directory);
    m_path = directory + QString("/scroll-%1.json").arg(QCoreApplication::applicationPid());
    window->installEventFilter(this);
    if (m_verboseTiming) {
        {
            std::lock_guard guard(timingHandlerMutex);
            timingTarget = this;
            previousHandler = qInstallMessageHandler(timingHandler);
        }
        // 沿用用户环境中的规则，仅在本次诊断进程增加Qt原生布局耗时输出。
        QLoggingCategory::setFilterRules(qEnvironmentVariable("QT_LOGGING_RULES") + "\nqt.scenegraph.time.renderloop.debug=true");
    }
#ifdef Q_OS_MACOS
    if (m_disclosureMode) startDrawableTiming();
#endif
    connect(window, &QQuickWindow::afterAnimating, this, [this] {
        // QML 只在 GUI 线程读取，渲染线程同步阶段复制本次快照。普通滚动诊断不承担遍历开销。
        QVector4D coverage(-1, -1, -1, -1);
        if (m_profileCoverage && m_view)
            QMetaObject::invokeMethod(m_view, "viewportCoverage", Qt::DirectConnection, Q_RETURN_ARG(QVector4D, coverage));
        std::lock_guard guard(m_mutex);
        m_guiCoverage = coverage;
        m_afterAnimating = timestamp();
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, this, [this] { presented(); }, Qt::DirectConnection);
    // 按需绘制的帧间隔包含空闲，不能据此判定掉帧；分别记录真实同步与渲染耗时。
    connect(window, &QQuickWindow::beforeSynchronizing, this, [this, window] {
        std::lock_guard guard(m_mutex);
        ++m_syncSequence;
        m_syncStarted = m_clock.nsecsElapsed() / 1000000.0;
        m_frameAfterAnimating = m_afterAnimating;
        m_frameCoverage = m_guiCoverage;
        // GUI在本帧同步阶段阻塞，仅将已进入同步的输入归属到该帧，不能借旧帧提前结算。
        m_syncedInputCount = m_inputCount;
        m_syncedDisclosureCount = m_disclosures.size();
        // 同步时GUI已阻塞，将视口与输入绑定到同一提交帧，不能在渲染线程读取后来的GUI状态。
        const auto *view = m_view.data();
        m_frameViewY = view ? view->property("contentY").toDouble() : 0;
        m_frameOriginY = view ? view->property("originY").toDouble() : 0;
        m_frameKinetic = view && view->property("pixelWheelMoving").toBool();
        m_framePrefetchDirection = view ? view->property("prefetchDirection").toInt() : 0;
        m_frameActive = window->isActive();
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterSynchronizing, this, [this] {
        const double now = m_clock.nsecsElapsed() / 1000000.0;
        std::lock_guard guard(m_mutex);
        m_frameSyncDuration = m_syncStarted >= 0 ? now - m_syncStarted : 0;
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
        m_frameRenderDuration = m_renderStarted >= 0 ? now - m_renderStarted : 0;
        if (m_renderStarted >= 0 && now - m_lastInput < 500 && m_renderDurations.size() < 4000)
            m_renderDurations.push_back(now - m_renderStarted);
    }, Qt::DirectConnection);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    // 最后一次采样在窗口仍有效时落盘，避免退出时遗漏刚刚提交的帧。
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { save(true); });
    m_saveTimer.start(1000);
}
ScrollDiagnostics::~ScrollDiagnostics()
{
    if (m_verboseTiming) {
        {
            std::lock_guard guard(timingHandlerMutex);
            timingTarget = nullptr;
            qInstallMessageHandler(previousHandler);
        }
    }
#ifdef Q_OS_MACOS
    if (m_disclosureMode) stopDrawableTiming();
#endif
}
bool ScrollDiagnostics::eventFilter(QObject *, QEvent *event)
{
    if (!m_disclosureMode && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)) {
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
    // 点击与双击也进入诊断，便于对照真实展开/收起；不记录鼠标位置或正文。
    const bool clickInput = event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
    const bool wheelInput = event->type() == QEvent::Wheel;
    const bool dragInput = event->type() == QEvent::MouseMove && (static_cast<QMouseEvent *>(event)->buttons() & Qt::LeftButton);
    if (!wheelInput && !dragInput && !clickInput) return false;
    const auto *wheel = wheelInput ? static_cast<QWheelEvent *>(event) : nullptr;
    // 触控板开始/结束阶段可只有相位而无位移，不应等待一帧不存在的内容变化。
    if (wheel && wheel->pixelDelta().isNull() && wheel->angleDelta().isNull()) return false;
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    std::lock_guard guard(m_mutex);
    if (now - m_lastInput > 350) m_activeStart = now;
    m_lastInput = now;
    ++m_inputCount;
    const auto *view = m_view.data();
    if (m_inputs.size() < 2000) m_inputs.append(QJsonObject{{"ms", now}, {"inputCount", m_inputCount}, {"kind", clickInput ? 3 : (wheelInput ? 1 : 2)},
        {"pixelY", wheel ? wheel->pixelDelta().y() : 0}, {"angleY", wheel ? wheel->angleDelta().y() : 0},
        {"deviceType", wheel && wheel->device() ? int(wheel->device()->type()) : 0},
        {"eventSource", wheel ? int(wheel->source()) : 0},
        {"sampleMs", m_clock.nsecsElapsed() / 1000000.0},
        {"active", qobject_cast<QQuickWindow *>(parent())->isActive()},
        {"viewFastScrolling", view ? view->property("fastScrolling").toBool() : false},
        {"cacheBufferPx", view ? view->property("cacheBuffer").toDouble() : 0},
        {"prefetchDirection", view ? view->property("prefetchDirection").toInt() : 0},
        {"prefetchSpeed", view ? view->property("prefetchSpeed").toDouble() : 0},
        {"prefetchAheadPx", view ? view->property("prefetchAhead").toInt() : 0},
        {"prefetchBehindPx", view ? view->property("prefetchBehind").toInt() : 0},
        {"viewPixelAligned", view ? view->property("pixelAligned").toBool() : false},
        {"viewMoving", view ? view->property("moving").toBool() : false},
        {"viewFlicking", view ? view->property("flicking").toBool() : false},
        {"viewY", view ? view->property("contentY").toDouble() : 0},
        {"originY", view ? view->property("originY").toDouble() : 0},
        {"phase", wheel ? static_cast<int>(wheel->phase()) : 0}});
    m_dirty = true;
    return false;
}
void ScrollDiagnostics::recordDispatch(double begin, double end)
{
    if (!m_disclosureMode) return;
    std::lock_guard guard(m_mutex);
    if (m_dispatches.size() < 2000) m_dispatches.append(QJsonObject{{"beginMs", begin}, {"endMs", end},
        {"durationMs", end - begin}, {"inputCount", m_inputCount}, {"disclosureCount", m_disclosures.size()}});
    m_dirty = true;
}
void ScrollDiagnostics::recordQtTiming(const QString &message)
{
    std::lock_guard guard(m_mutex);
    if (timestamp() - m_lastInput < 500 && m_qtTimings.size() < 10000)
        m_qtTimings.append(QJsonObject{{"ms", timestamp()}, {"disclosureCount", m_disclosures.size()}, {"message", message}});
    m_dirty = true;
}
void ScrollDiagnostics::recordWheelMotion(int phase, qreal position, qreal velocity, qreal destination, qreal input)
{
    if (!m_disclosureMode) return;
    // 0输入前、1目标更新、2动画帧、3停止、4输入内首步；只记录主聊天的滚动数值，不记录鼠标或聊天内容。
    std::lock_guard guard(m_mutex);
    if (m_wheelMotions.size() >= 4096) m_wheelMotions.removeFirst();
    m_wheelMotions.append(QJsonObject{{"ms", timestamp()}, {"phase", phase}, {"lastSyncSequence", qint64(m_syncSequence)}, {"inputCount", m_inputCount}, {"position", position},
        {"velocity", velocity}, {"destination", destination}, {"input", input}});
    m_dirty = true;
}
void ScrollDiagnostics::recordTextLayout(const QString &key, bool cached, double reserved, double actual,
                                         double beforeY, double afterY, double scrollDelta, double viewport, double rowDelta, int blockIndex)
{
    // 仅在诊断进程记录匿名块标识及几何数据，用于匹配同一块的冷加载和回滚。
    std::lock_guard guard(m_mutex);
    if (m_textLayouts.size() >= 2048) m_textLayouts.removeFirst();
    m_textLayouts.append(QJsonObject{{"block", QString::number(qHash(key))}, {"cached", cached},
        {"reserved", reserved}, {"actual", actual}, {"beforeY", beforeY}, {"afterY", afterY},
        {"rowDelta", rowDelta}, {"blockIndex", blockIndex}, {"scrollDelta", scrollDelta}, {"viewport", viewport}, {"ms", timestamp()}});
    m_dirty = true;
}
void ScrollDiagnostics::recordLayoutAnchor(qreal correction, qreal drift, bool usedBlock)
{
    // 仅记录像素补偿和残余误差，不记录消息身份或正文；保留最近256次，避免诊断无限增长。
    std::lock_guard guard(m_mutex);
    if (m_layoutAnchors.size() >= 256) m_layoutAnchors.removeFirst();
    m_layoutAnchors.append(QJsonObject{{"ms", timestamp()}, {"correctionPx", correction}, {"driftPx", drift}, {"usedBlock", usedBlock}});
    m_dirty = true;
}
void ScrollDiagnostics::recordContentPhase(QObject *loader, int kind, int phase)
{
    if (!m_disclosureMode || !loader) return;
    std::lock_guard guard(m_mutex);
    if (!m_loaders.contains(loader)) {
        m_loaders.insert(loader, ++m_nextLoader);
        connect(loader, &QObject::destroyed, this, [this, loader] { std::lock_guard guard(m_mutex); m_loaders.remove(loader); });
    }
    // Loader阶段是墙钟范围，包含异步调度；阶段4表示应用绑定完成，不能冒充解析CPU时间。
    if (m_contentPhases.size() < 10000) m_contentPhases.append(QJsonObject{{"ms", timestamp()},
        {"loader", m_loaders.value(loader)}, {"kind", kind}, {"phase", phase}, {"disclosureCount", m_disclosures.size()}});
    m_dirty = true;
}
void ScrollDiagnostics::recordDisclosure(int expansion)
{
    if (!m_disclosureMode) return;
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    std::lock_guard guard(m_mutex);
    // 只标记模型真实切换，空白点击不能作为展开响应样本；不保存消息身份或正文。
    if (m_disclosures.size() < 2000) m_disclosures.append(QJsonObject{{"ms", now},
        {"expansion", expansion}, {"inputCount", m_inputCount},
        {"motionSuppressed", parent()->property("disclosureMotionSuppressed").toBool()},
        {"activeDisclosureMotions", parent()->property("activeDisclosureMotions").toInt()},
        {"active", qobject_cast<QQuickWindow *>(parent())->isActive()}});
    m_lastInput = now;
    m_dirty = true;
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
            if (input.value("kind").toInt() == 2) m_dragLatency.push_back(delay);
            if (input.value("kind").toInt() == 1) m_wheelLatency.push_back(delay);
        }
        m_presentedInput = m_syncedInputCount;
        m_dirty = true;
    }
    double drawableMs = 0;
    int drawableCalls = 0;
#ifdef Q_OS_MACOS
    if (m_disclosureMode) for (double duration : takeDrawableTimings()) { drawableMs += duration; ++drawableCalls; }
#endif
    // 每次提交均排空缓冲获取样本，不能把空闲时期的获取成本算到下一次点击。
    if (now - m_lastInput < 1200 && m_frames.size() < 10000) {
        QJsonObject frame{{"syncSequence", qint64(m_syncSequence)}, {"afterAnimatingMs", m_frameAfterAnimating}, {"syncBeginMs", m_syncStarted},
            {"renderBeginMs", m_renderStarted}, {"drawableAcquireMs", drawableMs}, {"drawableCalls", drawableCalls}, {"ms", now}, {"syncedInputCount", m_syncedInputCount}, {"syncedDisclosureCount", m_syncedDisclosureCount},
            {"active", m_frameActive}, {"viewY", m_frameViewY}, {"originY", m_frameOriginY},
            {"kineticRunning", m_frameKinetic}, {"prefetchDirection", m_framePrefetchDirection},
            {"syncMs", m_frameSyncDuration}, {"renderMs", m_frameRenderDuration}};
        if (m_profileCoverage) {
            frame.insert("visibleRows", int(m_frameCoverage.x()));
            frame.insert("visibleProseSlots", int(m_frameCoverage.y()));
            frame.insert("boundProseRenderers", int(m_frameCoverage.z()));
            frame.insert("settledProseLayouts", int(m_frameCoverage.w()));
        }
        m_frames.append(frame);
        m_dirty = true;
    }
    m_lastFrame = now;
}
void ScrollDiagnostics::save(bool finalSnapshot)
{
    // 动画采样时不抓系统光标图片，且交互结束后才写盘，避免诊断自身制造主线程长任务。
    if (m_disclosureMode && !finalSnapshot) {
        std::lock_guard guard(m_mutex);
        if (m_clock.nsecsElapsed() / 1000000.0 - m_lastInput < 700) return;
    }
#ifdef Q_OS_MACOS
    const int systemCursor = m_disclosureMode ? -2 : macSystemCursorShape();
    {
        std::lock_guard guard(m_mutex);
        if (systemCursor != m_lastSystemCursor) { m_lastSystemCursor = systemCursor; m_dirty = true; }
    }
#endif
    QJsonArray textLayouts, inputs, cursorShapes, frames, disclosures, dispatches, contentPhases, qtTimings, layoutAnchors, wheelMotions;
    std::vector<double> intervals, latency, dragLatency, wheelLatency, syncDurations, renderDurations;
    int inputCount, inactiveHoverCount;
    {
        std::lock_guard guard(m_mutex);
        // 停止输入后继续保留短暂读数，用于核对视口是否仍在漂移，不制造动画或新绘制。
        if (!finalSnapshot && !m_dirty && m_clock.nsecsElapsed() / 1000000.0 - m_lastInput > 2000) return;
        m_dirty = false;
        textLayouts = m_textLayouts;
        dispatches = m_dispatches; contentPhases = m_contentPhases; qtTimings = m_qtTimings; layoutAnchors = m_layoutAnchors; wheelMotions = m_wheelMotions;
        disclosures = m_disclosures; frames = m_frames; inputs = m_inputs; intervals = m_intervals; latency = m_latency; inputCount = m_inputCount;
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
    const auto *window = qobject_cast<QQuickWindow *>(parent());
    const auto *view = window ? m_view.data() : nullptr;
    file.write(QJsonDocument(QJsonObject{{"schemaVersion", 6}, {"verboseTiming", m_verboseTiming}, {"pid", QCoreApplication::applicationPid()}, {"executable", QCoreApplication::applicationFilePath()}, {"renderLoop", qEnvironmentVariable("QSG_RENDER_LOOP", "default")}, {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"sampleMs", m_clock.nsecsElapsed() / 1000000.0},
        {"active", window && window->isActive()},
        {"viewFastScrolling", view ? view->property("fastScrolling").toBool() : false},
        {"cacheBufferPx", view ? view->property("cacheBuffer").toDouble() : 0},
        {"prefetchDirection", view ? view->property("prefetchDirection").toInt() : 0},
        {"prefetchSpeed", view ? view->property("prefetchSpeed").toDouble() : 0},
        {"prefetchAheadPx", view ? view->property("prefetchAhead").toInt() : 0},
        {"prefetchBehindPx", view ? view->property("prefetchBehind").toInt() : 0},
        {"viewPixelAligned", view ? view->property("pixelAligned").toBool() : false},
        {"viewMoving", view ? view->property("moving").toBool() : false},
        {"viewFlicking", view ? view->property("flicking").toBool() : false},
        {"viewY", view ? view->property("contentY").toDouble() : 0},
        {"originY", view ? view->property("originY").toDouble() : 0},
        {"frameBudgetMs", m_budget}, {"inputCount", inputCount}, {"frameIntervals", statistics(intervals)},
        {"frameIntervalsIncludeIdle", true}, {"syncDuration", statistics(syncDurations)}, {"renderDuration", statistics(renderDurations)},
        {"cursorShape", window ? int(window->cursor().shape()) : int(Qt::ArrowCursor)},
        {"cursorShapes", cursorShapes},
        {"inactiveHoverCount", inactiveHoverCount},
#ifdef Q_OS_MACOS
        {"systemCursorShape", systemCursor},
#endif
        {"inputToFrame", statistics(latency)}, {"dragInputToFrame", statistics(dragLatency)},
        {"wheelInputToFrame", statistics(wheelLatency)}, {"textLayouts", textLayouts}, {"inputs", inputs}, {"frameIntervalsMs", frameIntervals}, {"frames", frames}, {"disclosures", disclosures}, {"dispatches", dispatches}, {"contentPhases", contentPhases}, {"qtTimings", qtTimings}, {"layoutAnchors", layoutAnchors}, {"wheelMotions", wheelMotions}}).toJson());
    file.commit();
}
