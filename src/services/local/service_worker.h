#pragma once
#include "infrastructure/ipc/rpc_channel.h"
#include <QHash>
#include <QPointer>

namespace exagent {
class ServiceWorker final : public QObject {
    Q_OBJECT
public:
    ServiceWorker(QString socketName, QString token, QObject *parent = nullptr);
private:
    struct Task {
        QString conversationId, blockId, title;
        int step = 0, revision = 0;
        ipc::TaskState state = ipc::TaskState::Running;
        bool created = false, finished = false;
    };
    struct Event { QString method; QJsonObject payload; qint64 sequence; };
    void connectSocket();
    void reconnect();
    void request(const QString &id, const QString &method, const QJsonObject &params);
    void tick();
    bool publish(const QString &method, const QString &taskId, Task &task, const QJsonObject &data);
    void replay();
    bool resume(qint64 after);
    QString m_socketName, m_token, m_epoch, m_hello;
    QPointer<ipc::RpcChannel> m_channel;
    QTimer m_retry, m_tick;
    QHash<QString, Task> m_tasks;
    QList<Event> m_history;
    qint64 m_sequence = 0, m_acknowledged = 0, m_lastSent = 0;
    int m_attempts = 0;
    bool m_ready = false, m_closing = false;
};
int runServiceWorker(int argc, char **argv);
}
