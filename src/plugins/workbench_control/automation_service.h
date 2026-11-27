#pragma once
#include "control_host.h"
#include <QJsonArray>
#include <QTimer>

namespace exagent::control {
class AutomationService final : public QObject {
    Q_OBJECT
public:
    explicit AutomationService(Host &host);
private:
    bool save(QString *error);
    QJsonObject trigger(const QString &id, bool scheduled, QString *error);
    void tick();
    void ended(const QString &session, const QString &run, int state);
    Host &m_host;
    QString m_file, m_lastSchedulerError;
    QJsonArray m_jobs;
    QTimer m_timer, m_retry;
    bool m_valid = true;
};
}
