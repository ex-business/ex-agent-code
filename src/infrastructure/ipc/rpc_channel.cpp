#include "rpc_channel.h"
#include <QJsonDocument>
#include <QtEndian>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(rpcLog, "exagent.ipc.channel")

namespace exagent::ipc {
RpcChannel::RpcChannel(QLocalSocket *socket, QObject *parent) : QObject(parent), m_socket(socket)
{
    socket->setParent(this);
    socket->setReadBufferSize(MaxFrame + 4);
    m_clock.start();
    connect(socket, &QLocalSocket::readyRead, this, &RpcChannel::read);
    connect(socket, &QLocalSocket::bytesWritten, this, [this] { drain(); emit writableChanged(); });
    connect(socket, &QLocalSocket::disconnected, this, &RpcChannel::closed);
    connect(socket, &QLocalSocket::errorOccurred, this, [this] {
        if (m_socket->state() == QLocalSocket::UnconnectedState) closed();
    });
    m_deadlines.setInterval(200);
    connect(&m_deadlines, &QTimer::timeout, this, [this] {
        const auto keys = m_pending.keys();
        for (const auto &id : keys) if (m_pending.contains(id) && m_pending.value(id).deadline <= m_clock.elapsed()) {
            m_pending.remove(id);
            emit requestFailed(id, -32002, QStringLiteral("后台请求超时，执行结果可能未知"));
        }
    });
    m_deadlines.start();
    if (socket->bytesAvailable()) QTimer::singleShot(0, this, &RpcChannel::read);
}
bool RpcChannel::writable() const { return !m_closed && m_socket->state() == QLocalSocket::ConnectedState && m_queued + m_socket->bytesToWrite() < MaxQueue / 2; }
QString RpcChannel::request(const QString &method, const QJsonObject &params, int timeoutMs)
{
    if (m_pending.size() >= MaxPending) return {};
    const auto id = newId();
    if (!enqueue({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}, true)) return {};
    m_pending.insert(id, {m_clock.elapsed() + qBound(200, timeoutMs, 30000)});
    return id;
}
bool RpcChannel::notify(const QString &method, const QJsonObject &params, bool control) { return enqueue({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}}, control); }
bool RpcChannel::respond(const QString &id, const QJsonValue &result) { return enqueue({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}, true); }
bool RpcChannel::reject(const QString &id, int code, const QString &message) { return enqueue({{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}}, true); }
bool RpcChannel::enqueue(const QJsonObject &message, bool control)
{
    if (m_closed || m_socket->state() != QLocalSocket::ConnectedState) return false;
    const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (json.isEmpty() || json.size() > MaxFrame) return false;
    // 普通输出不能占满控制预留，取消和应答不被大输出饿死。事件之间始终FIFO。
    const qint64 limit = control ? MaxQueue : MaxQueue - MaxFrame;
    if (m_queued + m_socket->bytesToWrite() + json.size() + 4 > limit) return false;
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(json.size()), frame.data());
    frame.append(json);
    m_queued += frame.size();
    (control ? m_control : m_events).enqueue(std::move(frame));
    drain();
    return !m_closed;
}
void RpcChannel::drain()
{
    if (m_closed || m_draining) return;
    m_draining = true;
    while (m_socket->state() == QLocalSocket::ConnectedState && m_socket->bytesToWrite() < MaxFrame) {
        if (m_writing.isEmpty()) {
            if (m_control.isEmpty() && m_events.isEmpty()) break;
            m_writing = (!m_control.isEmpty() ? m_control : m_events).dequeue();
            m_writeOffset = 0;
        }
        const auto wrote = m_socket->write(m_writing.constData() + m_writeOffset, m_writing.size() - m_writeOffset);
        if (wrote < 0) { m_draining = false; abort(QStringLiteral("本机通信写入失败")); return; }
        if (wrote == 0) break;
        m_writeOffset += wrote; m_queued -= wrote;
        if (m_writeOffset == m_writing.size()) m_writing.clear();
    }
    m_draining = false;
}
void RpcChannel::read()
{
    m_readPending = false;
    if (m_closed) return;
    int frames = 0;
    qsizetype processedBytes = 0;
    while (!m_closed && frames < 64 && processedBytes < 2 * MaxFrame) {
        if (m_input.size() < 4) m_input.append(m_socket->read(4 - m_input.size()));
        if (m_input.size() < 4) return;
        const auto length = qFromBigEndian<quint32>(m_input.constData());
        if (length == 0 || length > MaxFrame) { abort(QStringLiteral("通信帧长度超出协议限制")); return; }
        const auto needed = qsizetype(length) + 4;
        if (m_input.size() < needed) m_input.append(m_socket->read(needed - m_input.size()));
        if (m_input.size() < needed) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_input.mid(4, length), &error);
        m_input.clear();
        if (error.error != QJsonParseError::NoError || !document.isObject()) { abort(QStringLiteral("通信帧不是合法的JSON对象")); return; }
        ++frames; processedBytes += needed;
        dispatch(document.object());
        if (m_socket->bytesAvailable() == 0) return;
    }
    // 一次最多分发64帧，给GUI事件、取消和超时处理留出执行机会。
    if (!m_closed && m_socket->bytesAvailable() && !m_readPending) {
        m_readPending = true;
        QTimer::singleShot(0, this, &RpcChannel::read);
    }
}
void RpcChannel::dispatch(const QJsonObject &message)
{
    if (message.value("jsonrpc") != QJsonValue("2.0")) { abort(QStringLiteral("JSON-RPC版本无效")); return; }
    const auto idValue = message.value("id");
    const auto id = idValue.toString();
    if (message.contains("id") && (!idValue.isString() || id.isEmpty() || id.size() > 128)) { abort(QStringLiteral("请求标识格式无效")); return; }
    if (message.contains("method")) {
        const auto method = message.value("method").toString();
        if (method.isEmpty() || method.size() > 128 || message.contains("result") || message.contains("error")) { abort(QStringLiteral("请求结构无效")); return; }
        if (message.contains("params") && !message.value("params").isObject()) {
            if (!id.isEmpty()) reject(id, -32602, QStringLiteral("参数必须为对象"));
            return;
        }
        if (id.isEmpty()) emit notificationReceived(method, message.value("params").toObject());
        else emit requestReceived(id, method, message.value("params").toObject());
        return;
    }
    if (id.isEmpty() || message.contains("result") == message.contains("error")) { abort(QStringLiteral("应答结构无效")); return; }
    if (!m_pending.contains(id)) return; // 已超时或重复应答不会再次驱动业务。
    if (message.contains("error")) {
        const auto error = message.value("error").toObject();
        if (!integer(error.value("code"), -2147483648LL, 2147483647LL) || !error.value("message").isString()) { abort(QStringLiteral("错误应答格式无效")); return; }
        m_pending.remove(id);
        emit requestFailed(id, error.value("code").toInt(), error.value("message").toString().left(1024));
    } else {
        m_pending.remove(id);
        emit responseReceived(id, message.value("result"));
    }
}
void RpcChannel::abort(const QString &reason)
{
    if (m_closed) return;
    qCWarning(rpcLog).noquote() << "本机协议连接中断：" << reason;
    emit protocolError(reason);
    m_socket->abort();
    closed();
}
void RpcChannel::closed()
{
    if (m_closed) return;
    m_closed = true;
    m_deadlines.stop();
    m_control.clear(); m_events.clear(); m_writing.clear(); m_input.clear(); m_queued = 0;
    const auto pending = m_pending.keys(); m_pending.clear();
    for (const auto &id : pending) emit requestFailed(id, -32001, QStringLiteral("后台连接中断，执行结果可能未知"));
    emit disconnected();
}
}
