#pragma once
#include "domain/ipc/protocol.h"
#include <QObject>
#include <QLocalSocket>
#include <QQueue>
#include <QHash>
#include <QTimer>
#include <QElapsedTimer>

namespace exagent::ipc {
// JSON-RPC 本机配置：4字节大端长度＋UTF-8对象，单帧1MiB；不接受批处理和数组参数。
class RpcChannel final : public QObject {
    Q_OBJECT
public:
    explicit RpcChannel(QLocalSocket *socket, QObject *parent = nullptr);
    QString request(const QString &method, const QJsonObject &params = {}, int timeoutMs = 5000);
    bool notify(const QString &method, const QJsonObject &params, bool control = false);
    bool respond(const QString &id, const QJsonValue &result);
    bool reject(const QString &id, int code, const QString &message);
    void abort(const QString &reason);
    QLocalSocket *socket() const { return m_socket; }
    bool writable() const;
signals:
    void requestReceived(QString id, QString method, QJsonObject params);
    void notificationReceived(QString method, QJsonObject params);
    void responseReceived(QString id, QJsonValue result);
    void requestFailed(QString id, int code, QString message);
    void disconnected();
    void protocolError(QString reason);
    void writableChanged();
private:
    struct Pending { qint64 deadline; };
    bool enqueue(const QJsonObject &message, bool control);
    void drain();
    void read();
    void dispatch(const QJsonObject &message);
    void closed();
    QLocalSocket *m_socket;
    QByteArray m_input, m_writing;
    qsizetype m_writeOffset = 0;
    QQueue<QByteArray> m_control, m_events;
    qint64 m_queued = 0;
    QHash<QString, Pending> m_pending;
    QTimer m_deadlines;
    QElapsedTimer m_clock;
    bool m_closed = false;
    bool m_readPending = false;
    bool m_draining = false;
};
}
