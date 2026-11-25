#include "service_worker.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QLoggingCategory>
#include <cstdio>
Q_LOGGING_CATEGORY(workerLog, "exagent.ipc.worker")

namespace exagent {
using namespace ipc;
ServiceWorker::ServiceWorker(QString socketName, QString token, bool agentMode, QObject *parent)
    : QObject(parent), m_socketName(std::move(socketName)), m_token(std::move(token)), m_epoch(newId())
{
    if (agentMode) {
        m_agent = new codex::Worker(this);
        connect(m_agent,&codex::Worker::available,this,&ServiceWorker::drainAgent);
    }
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &ServiceWorker::connectSocket);
    m_tick.setInterval(160);
    connect(&m_tick, &QTimer::timeout, this, &ServiceWorker::tick);
    m_tick.start();
    connectSocket();
}
void ServiceWorker::reconnect()
{
    if (m_closing || m_retry.isActive()) return;
    m_ready = false;
    if (++m_attempts > 6) {
        qCWarning(workerLog) << "后台重连次数已耗尽，保留宿主已有记录";
        QCoreApplication::exit(2); return;
    }
    m_retry.start(qMin(1000, 50 * (1 << (m_attempts - 1))));
}
void ServiceWorker::connectSocket()
{
    auto *socket = new QLocalSocket(this);
    connect(socket, &QLocalSocket::errorOccurred, this, [this, socket] {
        if ((!m_channel || m_channel->socket() != socket) && socket->state() == QLocalSocket::UnconnectedState) {
            socket->deleteLater(); reconnect();
        }
    });
    connect(socket, &QLocalSocket::connected, this, [this, socket] {
        auto *channel = new RpcChannel(socket, this);
        m_channel = channel;
        connect(channel, &RpcChannel::requestReceived, this, &ServiceWorker::request);
        connect(channel, &RpcChannel::notificationReceived, this, [this](const QString &method, const QJsonObject &params) {
            if (!m_ready || !m_channel) return;
            if (method != "stream.ack" || !integer(params.value("sequence"), m_acknowledged, m_sequence)) {
                m_channel->abort(QStringLiteral("流确认位置无效")); return;
            }
            m_acknowledged = qint64(params.value("sequence").toDouble());
            while (m_history.size() > ReplayHistory && m_history.first().sequence <= m_acknowledged) m_history.removeFirst();
            replay();
        });
        connect(channel, &RpcChannel::responseReceived, this, [this, channel](const QString &id, const QJsonValue &result) {
            if (m_channel != channel || id != m_hello) return;
            const auto object = result.toObject();
            if (!integer(object.value("version"), Version, Version) || !integer(object.value("afterSequence"), 0, m_sequence)) {
                channel->abort(QStringLiteral("握手应答无效")); return;
            }
            m_ready = true; m_attempts = 0;
            resume(qint64(object.value("afterSequence").toDouble()));
        });
        connect(channel, &RpcChannel::requestFailed, this, [this, channel](const QString &id, int, const QString &) {
            if (m_channel == channel && id == m_hello && channel->socket()->state() == QLocalSocket::ConnectedState)
                channel->abort(QStringLiteral("后台握手失败"));
        });
        connect(channel, &RpcChannel::disconnected, this, [this, channel] {
            if (m_channel == channel) { m_channel = nullptr; reconnect(); }
            channel->deleteLater();
        });
        connect(channel, &RpcChannel::writableChanged, this, &ServiceWorker::replay);
        m_hello = channel->request("protocol.hello", {{"version", Version}, {"token", m_token},
            {"producerId", m_agent ? "codex-agent" : "local-service"}, {"producerEpoch", m_epoch},
            {"capabilities", m_agent ? QJsonArray{"system.ping","stream.resume","agent.start","agent.stop","agent.approve","agent.steer"} : QJsonArray{"system.ping", "system.echo", "task.start", "task.cancel", "stream.resume"}},
            {"blockKinds", QJsonArray{int(BlockKind::Text)}}}, 3000);
        if (m_hello.isEmpty()) channel->abort(QStringLiteral("握手请求未能排队"));
    });
    socket->connectToServer(m_socketName);
}
bool ServiceWorker::resume(qint64 after)
{
    if (!m_history.isEmpty() && after < m_history.first().sequence - 1) {
        m_channel->notify("stream.gap", {{"afterSequence", double(after)}, {"firstAvailable", double(m_history.first().sequence)}}, true);
        return false;
    }
    m_acknowledged = after; m_lastSent = after;
    replay();
    return true;
}
void ServiceWorker::replay()
{
    if (!m_ready || !m_channel) return;
    for (const auto &event : m_history) {
        if (event.sequence <= m_lastSent) continue;
        if (!m_channel->writable() || !m_channel->notify(event.method, event.payload)) return;
        m_lastSent = event.sequence;
    }
}
bool ServiceWorker::publish(const QString &method, const QString &taskId, Task &task, const QJsonObject &data)
{
    if (!m_ready || !m_channel || m_sequence - m_acknowledged >= EventWindow || m_lastSent != m_sequence) return false;
    auto payload = data;
    payload.insert("revision", ++task.revision);
    payload.insert("conversationId", task.conversationId); payload.insert("taskId", taskId); payload.insert("blockId", task.blockId);
    return publishEvent(method,payload);
}
bool ServiceWorker::publishEvent(const QString &method,QJsonObject payload)
{
    if (!m_ready || !m_channel || m_sequence - m_acknowledged >= EventWindow || m_lastSent != m_sequence) return false;
    payload.insert("eventId",newId()); payload.insert("producerEpoch",m_epoch);
    payload.insert("sequence",double(++m_sequence));
    m_history.append({method, payload, m_sequence});
    while (m_history.size() > ReplayHistory && m_history.first().sequence <= m_acknowledged) m_history.removeFirst();
    replay();
    return true;
}
void ServiceWorker::drainAgent()
{
    if (!m_agent) return;
    while (!m_agent->events.isEmpty()) {
        auto next = m_agent->events.head();
        const auto bytes = next.take("_bytes").toInteger();
        if (!publishEvent("agent.event",next)) return;
        m_agent->queuedBytes -= bytes;
        m_agent->events.dequeue();
    }
}
void ServiceWorker::request(const QString &id, const QString &method, const QJsonObject &params)
{
    if (!m_channel) return;
    auto *channel = m_channel.data();
    if (!m_ready) { channel->reject(id, -32000, QStringLiteral("后台尚未完成握手")); return; }
    if (method == "system.ping") { channel->respond(id, QJsonObject{{"processId", double(QCoreApplication::applicationPid())}, {"epoch", m_epoch}}); return; }
    if (method == "system.echo") { channel->respond(id, QJsonObject{{"processId", double(QCoreApplication::applicationPid())}, {"payload", params}}); return; }
    if (method == "system.shutdown") {
        m_closing = true; m_tick.stop(); channel->respond(id, QJsonObject{{"status", 0}});
        QTimer::singleShot(150, qApp, [] { QCoreApplication::quit(); }); return;
    }
    if (method == "stream.resume") {
        if (!integer(params.value("afterSequence"), 0, m_sequence)) { channel->reject(id, -32602, QStringLiteral("恢复序列无效")); return; }
        const auto after = qint64(params.value("afterSequence").toDouble());
        if (!m_history.isEmpty() && after < m_history.first().sequence - 1) { channel->reject(id, -32009, QStringLiteral("恢复位置早于内存重放窗口")); return; }
        channel->respond(id, QJsonObject{{"afterSequence", double(after)}}); resume(after); return;
    }
    if (m_agent) {
        QString error;
        if (params["producerEpoch"].toString() != m_epoch || !m_agent->command(method,params,&error))
            channel->reject(id,-32602,error.isEmpty() ? QStringLiteral("后台代次已变化") : error);
        else channel->respond(id,QJsonObject{});
        return;
    }
    if ((method == "task.start" || method == "task.cancel") && params.value("producerEpoch").toString() != m_epoch) {
        channel->reject(id, -32007, QStringLiteral("后台代次不匹配，禁止重跑旧任务")); return;
    }
    if (method == "task.start") {
        if (!identifier(params.value("taskId")) || !identifier(params.value("blockId")) || !identifier(params.value("conversationId"))
            || !integer(params.value("kind"), 1, 1) || !params.value("title").isString() || params.value("title").toString().size() > 200) {
            channel->reject(id, -32602, QStringLiteral("任务标识、类型或标题无效")); return;
        }
        const auto taskId = params.value("taskId").toString();
        if (m_tasks.contains(taskId)) {
            const auto &task = m_tasks[taskId];
            if (task.blockId != params.value("blockId").toString() || task.conversationId != params.value("conversationId").toString() || task.title != params.value("title").toString()) {
                channel->reject(id, -32008, QStringLiteral("任务标识已用于其他内容")); return;
            }
            // 同一taskId的重试只返回现有任务，不能再次执行。
            channel->respond(id, QJsonObject{{"taskId", taskId}, {"status", int(task.state)}}); return;
        }
        if (m_tasks.size() >= 128) { channel->reject(id, -32010, QStringLiteral("后台任务容量已满，请结束本次后台服务后重试")); return; }
        m_tasks.insert(taskId, {params.value("conversationId").toString(), params.value("blockId").toString(), params.value("title").toString()});
        channel->respond(id, QJsonObject{{"taskId", taskId}, {"status", int(TaskState::Created)}}); return;
    }
    if (method == "task.cancel") {
        const auto taskId = params.value("taskId").toString();
        if (!m_tasks.contains(taskId)) { channel->reject(id, -32602, QStringLiteral("任务不存在")); return; }
        auto &task = m_tasks[taskId];
        if (!task.finished) task.state = TaskState::Cancelled;
        channel->respond(id, QJsonObject{{"taskId", taskId}, {"status", int(task.state)}}); return;
    }
    channel->reject(id, -32601, QStringLiteral("后台方法未注册"));
}
void ServiceWorker::tick()
{
    if (!m_ready || !m_channel) return;
    replay();
    if (m_agent) { drainAgent(); return; }
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        auto &task = it.value();
        if (task.finished) continue;
        if (!task.created) {
            if (publish("block.created", it.key(), task, {{"kind", int(BlockKind::Text)}, {"title", task.title}, {"taskState", int(TaskState::Running)},
                {"text", QStringLiteral("独立后台进程 %1 已接收通信任务。\n").arg(QCoreApplication::applicationPid())}})) task.created = true;
            continue;
        }
        const bool cancelled = task.state == TaskState::Cancelled;
        const bool complete = task.step >= 10;
        const auto state = cancelled ? TaskState::Cancelled : complete ? TaskState::Complete : TaskState::Running;
        const auto text = cancelled ? QStringLiteral("任务已取消，已有输出保留。\n") : complete ? QStringLiteral("通信任务完成，事件已同步到聊天。\n")
            : QStringLiteral("已完成第 %1 / 10 次后台处理。\n").arg(task.step + 1);
        if (publish("block.patch", it.key(), task, {{"taskState", int(state)}, {"appendText", text}})) {
            task.state = state; ++task.step;
            task.finished = cancelled || complete;
        }
    }
}
int runServiceWorker(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const int option = args.indexOf("--socket");
    if (option < 0 || option + 1 >= args.size()) { qCritical("缺少后台通信端点"); return 2; }
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    const auto token = QString::fromUtf8(input.readLine(256)).trimmed();
    if (token.size() != 64) { qCritical("缺少有效的后台认证材料"); return 2; }
    app.setOrganizationName("EX Business"); app.setApplicationName("EX Agent Code");
    ServiceWorker worker(args[option + 1], token, args.contains("--agent-worker"));
    return app.exec();
}
}
