#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTimer>
#include <mutex>
#include <vector>
class QQuickWindow;

// 仅 --profile-scroll 启用；记录帧间隔与输入类型，不记录聊天或键盘内容。
class ScrollDiagnostics final : public QObject {
public:
    explicit ScrollDiagnostics(QQuickWindow *window);
    ~ScrollDiagnostics() override;
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void presented();
    void save();
    QElapsedTimer m_clock;
    QTimer m_saveTimer;
    std::mutex m_mutex;
    QJsonArray m_inputs;
    std::vector<double> m_intervals;
    std::vector<double> m_latency;
    std::vector<double> m_dragLatency;
    std::vector<double> m_wheelLatency;
    double m_lastInput = -1000;
    double m_activeStart = 0;
    double m_lastFrame = -1;
    double m_budget = 16.67;
    int m_inputCount = 0;
    int m_presentedInput = 0;
    bool m_dirty = false;
    QString m_path;
};
