#pragma once
#include "codex_run.h"
#include <QQueue>
namespace exagent::codex {
class Worker final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    bool command(const QString &method, const QJsonObject &params, QString *error);
    QQueue<QJsonObject> events;
    qsizetype queuedBytes = 0;
signals:
    void available();

private:
    QHash<QString, Run *> m_runs;
    QSet<QString> m_sessions;
};
}
