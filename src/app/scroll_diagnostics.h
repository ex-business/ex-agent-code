#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QHash>
#include <QTimer>
#include <QPointer>
#include <mutex>
#include <vector>
class QQuickWindow;
class QQuickItem;

// 仅 --profile-scroll 启用；记录帧间隔与输入类型，不记录聊天或键盘内容。
class ScrollDiagnostics final : public QObject {
    Q_OBJECT
public:
    explicit ScrollDiagnostics(QQuickWindow *window);
    ~ScrollDiagnostics() override;
    void recordDisclosure(int expansion);
    double timestamp() const { return m_clock.nsecsElapsed() / 1000000.0; }
    void recordDispatch(double begin, double end);
    void recordQtTiming(const QString &message);
    Q_INVOKABLE void recordWheelMotion(int phase, qreal position, qreal velocity, qreal destination, qreal input);
    Q_INVOKABLE void recordTextLayout(const QString &key, bool cached, double reserved, double actual, double beforeY, double afterY, double scrollDelta, double viewport, double rowDelta, int blockIndex);
    Q_INVOKABLE void recordLayoutAnchor(qreal correction, qreal drift, bool usedBlock);
    Q_INVOKABLE void recordContentPhase(QObject *loader, int kind, int phase);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void presented();
    void save();
    QElapsedTimer m_clock;
    QTimer m_saveTimer;
    std::mutex m_mutex;
    QJsonArray m_textLayouts;
    QJsonArray m_inputs;
    QJsonArray m_cursorShapes;
    QJsonArray m_frames;
    QJsonArray m_disclosures;
    QJsonArray m_dispatches, m_contentPhases, m_qtTimings, m_layoutAnchors, m_wheelMotions;
    QHash<QObject *, int> m_loaders;
    int m_nextLoader = 0;
    double m_afterAnimating = -1;
    double m_frameAfterAnimating = -1;
    double m_frameViewY = 0, m_frameOriginY = 0;
    bool m_frameActive = false, m_frameKinetic = false;
    int m_framePrefetchDirection = 0;
    bool m_cursorPending = false;
    int m_inactiveHoverCount = 0;
    int m_lastSystemCursor = -2;
    std::vector<double> m_intervals;
    std::vector<double> m_latency;
    std::vector<double> m_syncDurations;
    std::vector<double> m_renderDurations;
    quint64 m_syncSequence = 0;
    double m_syncStarted = -1;
    double m_frameSyncDuration = 0;
    double m_frameRenderDuration = 0;
    double m_renderStarted = -1;
    std::vector<double> m_dragLatency;
    std::vector<double> m_wheelLatency;
    double m_lastInput = -1000;
    double m_activeStart = 0;
    double m_lastFrame = -1;
    double m_budget = 16.67;
    int m_inputCount = 0;
    int m_presentedInput = 0;
    int m_syncedInputCount = 0;
    int m_syncedDisclosureCount = 0;
    bool m_dirty = false;
    QString m_path;
    QPointer<QQuickItem> m_view;
    bool m_verboseTiming = false;
    bool m_disclosureMode = false;
};
