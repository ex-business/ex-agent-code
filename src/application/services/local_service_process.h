#pragma once
#include "infrastructure/ipc/rpc_channel.h"
#include <QLocalServer>
#include <QProcess>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <memory>
#include <functional>

namespace exagent {
struct ServiceLaunch {
    QString program;
    QStringList arguments{QStringLiteral("--ipc-service")};
    QString producerId = QStringLiteral("local-service");
    QStringList requiredCapabilities{QStringLiteral("system.ping"), QStringLiteral("task.start"), QStringLiteral("task.cancel"), QStringLiteral("stream.resume")};
};
// 应用层拥有后台进程和连接；传输只承载协议，不触碰QML控件。
class LocalServiceProcess final : public QObject {
    Q_OBJECT
public:
    using EventSink = std::function<bool(const QString &, const QJsonObject &, QString *)>;
    explicit LocalServiceProcess(QObject *parent = nullptr);
    LocalServiceProcess(ServiceLaunch launch, QObject *parent);
    ~LocalServiceProcess() override;
    ipc::ServiceState state() const { return m_state; }
    void setEventSink(EventSink sink) { m_sink = std::move(sink); }
    void start();
    void stop();
    QString request(const QString &method, const QJsonObject &params = {});
    qint64 processId() const { return m_process.processId(); }
    QString epoch() const { return m_epoch; }
signals:
    void stateChanged();
    void ready();
    void failure(QString reason);
    void response(QString id, QJsonValue result);
    void requestFailed(QString id, int code, QString reason);
private:
    void setState(ipc::ServiceState state);
    void acceptConnection();
    void handleRequest(ipc::RpcChannel *channel, const QString &id, const QString &method, const QJsonObject &params);
    void handleEvent(const QString &method, const QJsonObject &params);
    void fail(const QString &reason);
    ServiceLaunch m_launch;
    std::unique_ptr<QTemporaryDir> m_endpointDirectory;
    QLocalServer m_server;
    QProcess m_process;
    QTimer m_watchdog, m_heartbeat;
    QPointer<ipc::RpcChannel> m_channel;
    QSet<ipc::RpcChannel *> m_candidates;
    QSet<QString> m_pingRequests;
    QString m_token, m_epoch;
    qint64 m_sequence = 0;
    quint64 m_launchGeneration = 0;
    ipc::ServiceState m_state = ipc::ServiceState::Stopped;
    bool m_stopping = false;
    EventSink m_sink;
};
}
