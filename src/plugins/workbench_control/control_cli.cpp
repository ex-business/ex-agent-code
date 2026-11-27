#include "control_cli.h"
#include "infrastructure/ipc/rpc_channel.h"
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace exagent::control {
QString descriptorPath() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/control.json"; }
int runCli(QStringList args)
{
    args.removeAll("--json");
    QString endpoint = descriptorPath();
    const int option = args.indexOf("--endpoint");
    if (option >= 0 && option + 1 < args.size()) { endpoint = args[option + 1]; args.removeAt(option + 1); args.removeAt(option); }
    const auto method = args.isEmpty() ? QStringLiteral("help") : args.takeFirst();
    if (method == "help" || method == "--help") {
        std::puts("ex-agent-code control <method> [JSON | --stdin] [--json]\n"
                  "  capabilities | projects.list | projects.create\n"
                  "  workspaces.list | workspaces.bind | workspaces.checkpoint\n"
                  "  sessions.list | sessions.create | sessions.select | sessions.show | sessions.send | runs.show\n"
                  "  automations.list | automations.get | automations.create | automations.set | automations.run | automations.runs\n"
                  "  skills.list | skills.get | skills.run\n"
                  "  mobile.status | mobile.start | mobile.stop | mobile.offer | mobile.devices | mobile.revoke\n"
                  "  ssh.hosts | ssh.add | ssh.probe | ssh.bind | ssh.check | ssh.worktree | ssh.workspaces\n"
                  "  ssh.run | ssh.read | ssh.runs | ssh.request | ssh.disconnect\n"
                  "  bots.list | bots.create | bots.get | bots.prepare | bots.update | bots.set | bots.open | bots.new-chat | bots.send\n"
                  "  plugins.list | plugins.inspect | plugins.install | plugins.upgrade | plugins.get\n"
                  "  plugins.approve | plugins.enable | plugins.rollback | plugins.invoke | plugins.panels | plugins.panel\n"
                  "  --endpoint <file>  连接指定隔离实例的 control.json\n"
                  "  参数及返回值使用 JSON，对象 ID 由 list 命令获取。");
        return 0;
    }
    auto fail = [](int code, const QString &message) {
        const QJsonObject body{{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
        std::puts(QJsonDocument(body).toJson(QJsonDocument::Compact).constData());
        return 2;
    };
    if (args.size() > 1) return fail(-32602, QStringLiteral("请提供单个 JSON 对象或 --stdin。"));
    QByteArray input;
    if (args.value(0) == "--stdin") {
        QFile stream;
        if (!stream.open(stdin, QIODevice::ReadOnly)) return fail(-32602, QStringLiteral("无法读取标准输入。"));
        input = stream.read(65537);
    } else input = args.value(0, "{}").toUtf8();
    const auto document = QJsonDocument::fromJson(input);
    if (input.size() > 65536 || !document.isObject()) return fail(-32602, QStringLiteral("参数必须是 64 KiB 内的 JSON 对象。"));
    QFile file(endpoint);
    const QFileInfo info(endpoint);
    if (!info.isAbsolute() || info.isSymLink() || info.size() > 4096 || !file.open(QIODevice::ReadOnly))
        return fail(-32001, QStringLiteral("未找到可用的 EX 桌面实例，请先打开应用。"));
    const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (info.permissions() & exposed) return fail(-32001, QStringLiteral("桌面连接文件权限过宽，请重启 EX 修复连接文件。"));
#ifdef Q_OS_UNIX
    if (info.ownerId() != uint(getuid())) return fail(-32001, QStringLiteral("桌面连接文件不属于当前用户。"));
#endif
    const auto descriptor = QJsonDocument::fromJson(file.read(4097)).object();
    if (descriptor["version"].toDouble(-1) != 1 || descriptor["token"].toString().size() != 64
        || !descriptor["socket"].toString().startsWith("ex-control-")) return fail(-32001, QStringLiteral("桌面连接描述无效。"));
    auto *socket = new QLocalSocket;
    ipc::RpcChannel channel(socket);
    QEventLoop loop;
    QJsonObject output;
    int result = 2;
    QString hello, request;
    QObject::connect(socket, &QLocalSocket::connected, &loop, [&] {
        hello = channel.request("control.hello", {{"version", 1}, {"token", descriptor["token"]}});
        if (hello.isEmpty()) loop.quit();
    });
    QObject::connect(&channel, &ipc::RpcChannel::responseReceived, &loop, [&](const QString &id, const QJsonValue &value) {
        if (id == hello) {
            request = channel.request(method, document.object(), 10000);
            if (request.isEmpty()) loop.quit();
        } else if (id == request) {
            output = {{"ok", true}, {"result", value}};
            result = 0;
            loop.quit();
        }
    });
    QObject::connect(&channel, &ipc::RpcChannel::requestFailed, &loop, [&](const QString &, int code, const QString &message) {
        output = {{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
        loop.quit();
    });
    QObject::connect(&channel, &ipc::RpcChannel::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(socket, &QLocalSocket::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    socket->connectToServer(descriptor["socket"].toString());
    loop.exec();
    if (output.isEmpty()) return fail(-32002, QStringLiteral("桌面连接中断或超时，修改结果待核对，请勿自动重放。"));
    std::puts(QJsonDocument(output).toJson(QJsonDocument::Compact).constData());
    return result;
}
}
