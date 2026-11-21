#include "scroll_diagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QMouseEvent>
#include <algorithm>

ScrollDiagnostics::ScrollDiagnostics(QQuickWindow *window) : QObject(window)
{
    m_clock.start();
    if (window->screen() && window->screen()->refreshRate() > 0) m_budget = 1000.0 / window->screen()->refreshRate();
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/diagnostics";
    QDir().mkpath(directory);
    m_path = directory + QString("/scroll-%1.json").arg(QCoreApplication::applicationPid());
    window->installEventFilter(this);
    connect(window, &QQuickWindow::frameSwapped, this, [this] { presented(); }, Qt::DirectConnection);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_saveTimer.start(1000);
}
ScrollDiagnostics::~ScrollDiagnostics() { save(); }
bool ScrollDiagnostics::eventFilter(QObject *, QEvent *event)
{
    const bool wheelInput = event->type() == QEvent::Wheel;
    const bool dragInput = event->type() == QEvent::MouseMove && (static_cast<QMouseEvent *>(event)->buttons() & Qt::LeftButton);
    if (!wheelInput && !dragInput) return false;
    const auto *wheel = wheelInput ? static_cast<QWheelEvent *>(event) : nullptr;
    const double now = m_clock.nsecsElapsed() / 1000000.0;
    std::lock_guard guard(m_mutex);
    if (now - m_lastInput > 350) m_activeStart = now;
    m_lastInput = now;
    ++m_inputCount;
    if (m_inputs.size() < 2000) m_inputs.append(QJsonObject{{"ms", now}, {"kind", wheelInput ? 1 : 2},
        {"pixelY", wheel ? wheel->pixelDelta().y() : 0}, {"angleY", wheel ? wheel->angleDelta().y() : 0},
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
        for (int index = m_presentedInput; index < m_inputCount && index < m_inputs.size() && m_latency.size() < 2000; ++index) {
            const auto input = m_inputs.at(index).toObject();
            const auto delay = now - input.value("ms").toDouble();
            m_latency.push_back(delay);
            (input.value("kind").toInt() == 2 ? m_dragLatency : m_wheelLatency).push_back(delay);
        }
        m_presentedInput = m_inputCount;
        m_dirty = true;
    }
    m_lastFrame = now;
}
void ScrollDiagnostics::save()
{
    QJsonArray inputs;
    std::vector<double> intervals, latency, dragLatency, wheelLatency;
    int inputCount;
    {
        std::lock_guard guard(m_mutex);
        if (!m_dirty) return;
        m_dirty = false;
        inputs = m_inputs; intervals = m_intervals; latency = m_latency; inputCount = m_inputCount;
        dragLatency = m_dragLatency; wheelLatency = m_wheelLatency;
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
    file.write(QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"renderLoop", qEnvironmentVariable("QSG_RENDER_LOOP", "default")}, {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"frameBudgetMs", m_budget}, {"inputCount", inputCount}, {"frameIntervals", statistics(intervals)},
        {"inputToFrame", statistics(latency)}, {"dragInputToFrame", statistics(dragLatency)},
        {"wheelInputToFrame", statistics(wheelLatency)}, {"inputs", inputs}, {"frameIntervalsMs", frameIntervals}}).toJson());
    file.commit();
}
