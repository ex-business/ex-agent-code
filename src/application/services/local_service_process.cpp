#include "local_service_process.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QRandomGenerator>
Q_LOGGING_CATEGORY(serviceLog, "exagent.ipc.process")

namespace exagent {
using namespace ipc;
LocalServiceProcess::LocalServiceProcess(QObject *parent) : LocalServiceProcess(ServiceLaunch{}, parent) {}
LocalServiceProcess::LocalServiceProcess(ServiceLaunch launch, QObject *parent) : QObject(parent), m_launch(std::move(launch))
{
    if (m_launch.program.isEmpty()) m_launch.program = QCoreApplication::applicationFilePath();
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(4);
    connect(&m_server, &QLocalServer::newConnection, this, &LocalServiceProcess::acceptConnection);
    m_watchdog.setSingleShot(true); m_watchdog.setInterval(7000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] { fail(QStringLiteral("后台启动或重连超时")); });
    m_heartbeat.setInterval(2000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (!m_channel || m_state != ServiceState::Ready) return;
        const auto id = m_channel->request("system.ping", {}, 3000);
        if (id.isEmpty()) { fail(QStringLiteral("后台控制队列已满")); return; }
        m_pingRequests.insert(id);
    });
    connect(&m_process, &QProcess::started, this, [this] {
        // 随机认证材料只走本次子进程标准输入，不进入参数、环境变量或日志。
        m_process.write(m_token.toUtf8() + '\n');
        m_process.closeWriteChannel();
        qCInfo(serviceLog) << "后台进程已启动，进程ID：" << m_process.processId();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_stopping && error == QProcess::FailedToStart) fail(QStringLiteral("后台进程无法启动：") + m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_watchdog.stop(); m_heartbeat.stop(); m_server.close();
        if (m_channel) m_channel->abort(QStringLiteral("后台进程已退出"));
        if (m_stopping) { m_stopping = false; setState(ServiceState::Stopped); }
        else fail(QStringLiteral("后台进程退出，退出码 %1，退出类型 %2").arg(code).arg(int(status)));
    });
    // 当前服务的标准输出不承载协议，且不允许未消费输出无限增长。
    m_process.setStandardOutputFile(QProcess::nullDevice());
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const auto output = m_process.readAllStandardError();
        if (!output.isEmpty()) qCWarning(serviceLog).noquote() << "后台诊断：" << QString::fromUtf8(output).left(2048).trimmed();
    });
}
LocalServiceProcess::~LocalServiceProcess()
{
    m_stopping = true;
    m_server.close();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(1000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
}
void LocalServiceProcess::setState(ServiceState state) { if (m_state != state) { m_state = state; emit stateChanged(); } }
void LocalServiceProcess::start()
{
    if (m_process.state() != QProcess::NotRunning) return;
    ++m_launchGeneration;
    m_stopping = false; m_sequence = 0; m_epoch.clear(); m_pingRequests.clear();
    m_token.clear();
    for (int i = 0; i < 8; ++i) m_token += QString::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    m_server.close();
    setState(ServiceState::Starting);
    const auto stale = m_candidates;
    for (auto *channel : stale) channel->abort(QStringLiteral("后台服务已重新启动"));
    m_candidates.clear();
    QString name = "eac-" + newId();
#ifdef Q_OS_UNIX
    // macOS不执行socket文件权限，必须额外用仅所有者可遍历的目录保护端点。
    m_endpointDirectory = std::make_unique<QTemporaryDir>(QDir::temp().filePath("eac-XXXXXX"));
    if (!m_endpointDirectory->isValid() || !QFile::setPermissions(m_endpointDirectory->path(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        fail(QStringLiteral("无法建立私有后台通信目录")); return;
    }
    name = m_endpointDirectory->filePath("rpc");
#endif
    // 服务启动配置来自宿主注册，绝不将后台事件或聊天文本拼成Shell命令。
    if (!QFileInfo(m_launch.program).isAbsolute() || !QFileInfo(m_launch.program).isExecutable() || !identifier(m_launch.producerId)) {
        fail(QStringLiteral("后台启动配置无效")); return;
    }
    if (!m_server.listen(name)) { fail(QStringLiteral("无法建立本机通信端点：") + m_server.errorString()); return; }
    setState(ServiceState::Starting); m_watchdog.start();
    m_process.setProgram(m_launch.program);
    m_process.setArguments(m_launch.arguments + QStringList{"--socket", m_server.fullServerName()});
    m_process.start();
}
void LocalServiceProcess::stop()
{
    m_stopping = true;
    m_watchdog.stop(); m_heartbeat.stop();
    if (m_channel) m_channel->request("system.shutdown", {}, 1000);
    if (m_process.state() == QProcess::NotRunning) { m_server.close(); m_stopping = false; setState(ServiceState::Stopped); return; }
    // 旧停止任务不能干预同一宿主对象随后启动的新进程，即使新进程也正在退出。
    const auto generation = m_launchGeneration;
    QTimer::singleShot(1200, this, [this, generation] {
        if (m_launchGeneration == generation && m_stopping && m_process.state() != QProcess::NotRunning) m_process.terminate();
    });
    QTimer::singleShot(2200, this, [this, generation] {
        if (m_launchGeneration == generation && m_stopping && m_process.state() != QProcess::NotRunning) m_process.kill();
    });
}
QString LocalServiceProcess::request(const QString &method, const QJsonObject &params)
{
    if (m_state != ServiceState::Ready || !m_channel) return {};
    auto scoped = params;
    // 有副作用的请求绑定已协商代次，不能在新进程里自动重跑旧命令。
    if (method == "task.start" || method == "task.cancel") scoped.insert("producerEpoch", m_epoch);
    return m_channel->request(method, scoped);
}
void LocalServiceProcess::acceptConnection()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_candidates.size() >= 4 || m_channel) { socket->abort(); socket->deleteLater(); continue; }
        auto *channel = new RpcChannel(socket, this);
        m_candidates.insert(channel);
        connect(channel, &RpcChannel::requestReceived, this, [this, channel](const QString &id, const QString &method, const QJsonObject &params) { handleRequest(channel, id, method, params); });
        connect(channel, &RpcChannel::notificationReceived, this, [this, channel](const QString &method, const QJsonObject &params) {
            if (m_channel != channel) { channel->abort(QStringLiteral("未握手的连接不能发送事件")); return; }
            handleEvent(method, params);
        });
        connect(channel, &RpcChannel::responseReceived, this, [this, channel](const QString &id, const QJsonValue &result) {
            if (m_channel != channel) return;
            if (m_pingRequests.remove(id)) return;
            emit response(id, result);
        });
        connect(channel, &RpcChannel::requestFailed, this, [this, channel](const QString &id, int code, const QString &reason) {
            if (m_channel != channel) return;
            if (m_pingRequests.remove(id)) { if (!m_stopping) channel->abort(QStringLiteral("后台心跳超时")); return; }
            emit requestFailed(id, code, reason);
        });
        connect(channel, &RpcChannel::disconnected, this, [this, channel] {
            m_candidates.remove(channel);
            if (m_channel == channel) {
                m_channel = nullptr; m_heartbeat.stop(); m_pingRequests.clear();
                if (!m_stopping && m_state != ServiceState::Failed && m_process.state() != QProcess::NotRunning) {
                    setState(ServiceState::Reconnecting); m_watchdog.start();
                }
            }
            channel->deleteLater();
        });
        QTimer::singleShot(3000, channel, [this, channel] { if (m_channel != channel) channel->abort(QStringLiteral("协议握手超时")); });
    }
}
void LocalServiceProcess::handleRequest(RpcChannel *channel, const QString &id, const QString &method, const QJsonObject &params)
{
    if (m_channel && m_channel != channel) { channel->abort(QStringLiteral("已有已认证的后台连接")); return; }
    if (m_channel == channel) { channel->reject(id, -32601, QStringLiteral("宿主不支持该请求")); return; }
    if (method != "protocol.hello" || !integer(params.value("version"), Version, Version)
        || params.value("token").toString() != m_token || !identifier(params.value("producerEpoch"))
        || params.value("producerId").toString() != m_launch.producerId || !params.value("capabilities").isArray()) {
        channel->abort(QStringLiteral("后台认证或协议版本不匹配")); return;
    }
    const auto epoch = params.value("producerEpoch").toString();
    if (!m_epoch.isEmpty() && m_epoch != epoch) { channel->abort(QStringLiteral("生产者代次意外变化，拒绝混用事件")); return; }
    const auto capabilities = params.value("capabilities").toArray();
    for (const auto &required : m_launch.requiredCapabilities) if (!capabilities.contains(required)) {
        channel->abort(QStringLiteral("后台缺少必要协议能力")); return;
    }
    m_epoch = epoch; m_channel = channel; m_candidates.remove(channel);
    if (!channel->respond(id, QJsonObject{{"version", Version}, {"afterSequence", double(m_sequence)}, {"ackMode", "memory"}})) {
        fail(QStringLiteral("握手应答无法写入")); return;
    }
    m_watchdog.stop(); m_heartbeat.start(); setState(ServiceState::Ready);
    qCInfo(serviceLog).noquote() << "后台协议已就绪，代次：" << epoch << "已接收序列：" << m_sequence;
    emit ready();
}
void LocalServiceProcess::handleEvent(const QString &method, const QJsonObject &params)
{
    if (!m_channel || m_stopping) return;
    if (method == "stream.gap") { fail(QStringLiteral("断线位置早于后台重放窗口，已有内容已保留，请重新启动任务")); return; }
    if (!method.startsWith("block.") || params.value("producerEpoch").toString() != m_epoch
        || !identifier(params.value("eventId")) || !integer(params.value("sequence"), 1, 9007199254740991LL)) {
        fail(QStringLiteral("后台事件标识或代次无效")); return;
    }
    const qint64 sequence = qint64(params.value("sequence").toDouble());
    if (sequence <= m_sequence) { m_channel->notify("stream.ack", {{"sequence", double(m_sequence)}}, true); return; }
    if (sequence != m_sequence + 1) { fail(QStringLiteral("后台事件存在缺口，已暂停接收以保留现有内容")); return; }
    QString error;
    if (!m_sink || !m_sink(method, params, &error)) { fail(QStringLiteral("后台内容事件被拒绝：") + error); return; }
    m_sequence = sequence;
    if (!m_channel || m_stopping) return;
    if (!m_channel->notify("stream.ack", {{"sequence", double(sequence)}}, true)) fail(QStringLiteral("事件确认队列已满"));
}
void LocalServiceProcess::fail(const QString &reason)
{
    if (m_state == ServiceState::Failed || m_stopping) return;
    setState(ServiceState::Failed); m_watchdog.stop(); m_heartbeat.stop();
    qCWarning(serviceLog).noquote() << "后台服务异常：" << reason;
    emit failure(reason);
    if (m_channel) m_channel->abort(reason);
    if (m_process.state() != QProcess::NotRunning) {
        const auto pid = m_process.processId();
        m_process.terminate();
        QTimer::singleShot(2000, this, [this, pid] {
            if (m_state == ServiceState::Failed && m_process.processId() == pid && m_process.state() != QProcess::NotRunning) m_process.kill();
        });
    }
    m_server.close();
}
}
