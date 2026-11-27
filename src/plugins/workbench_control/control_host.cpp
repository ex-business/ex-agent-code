#include "control_host.h"
#include "automation_service.h"
#include "plugins/plugin_lifecycle/plugin_manager.h"
#include "infrastructure/ipc/rpc_channel.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUuid>
#include <memory>

Q_LOGGING_CATEGORY(controlLog, "exagent.control")
namespace exagent::control {
Host::Host(ChatController &controller, QObject *parent) : QObject(parent), chat(controller)
{
    if (!chat.projects()->writable()) return;
    m_descriptor = descriptorPath();
    if (QFileInfo(m_descriptor).isSymLink()) return;
    QByteArray secret;
    for (int i = 0; i < 8; ++i) secret.append(QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0'));
    m_token = QString::fromLatin1(secret);
    registerCore(*this);
    registerSkills(*this);
    new AutomationService(*this);
    add("capabilities", {}, [this](const QJsonObject &, QString *) {
        auto methods = m_methods.keys(); methods.sort();
        QJsonObject schemas;
        for (const auto &method : methods) {
            auto fields = m_methods.value(method).fields.values(); fields.sort();
            schemas.insert(method, QJsonObject{{"fields", QJsonArray::fromStringList(fields)}});
        }
        return QJsonObject{{"version", 1}, {"methods", QJsonArray::fromStringList(methods)}, {"schemas", schemas}};
    });
    new plugins::Manager(*this);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (m_connections >= 8) { socket->abort(); socket->deleteLater(); continue; }
            ++m_connections;
            auto *channel = new ipc::RpcChannel(socket, this);
            auto authenticated = std::make_shared<bool>(false);
            connect(channel, &ipc::RpcChannel::disconnected, this, [this, channel] { --m_connections; channel->deleteLater(); });
            QTimer::singleShot(3000, channel, [channel, authenticated] {
                if (!*authenticated) channel->abort(QStringLiteral("桌面控制认证超时。"));
            });
            QTimer::singleShot(30000, channel, [channel] { channel->abort(QStringLiteral("桌面控制连接已到期。")); });
            connect(channel, &ipc::RpcChannel::requestReceived, this,
                [this, channel, authenticated](const QString &id, const QString &method, const QJsonObject &params) {
                    if (!*authenticated) {
                        if (method != "control.hello" || params["version"].toDouble(-1) != 1
                            || params["token"].toString() != m_token) {
                            channel->abort(QStringLiteral("桌面控制认证失败。")); return;
                        }
                        *authenticated = true;
                        channel->respond(id, QJsonObject{{"version", 1}});
                        return;
                    }
                    QString error;
                    int code = -32000;
                    const auto result = invoke(method, params, &error, &code);
                    if (error.isEmpty()) channel->respond(id, result);
                    else channel->reject(id, code, error);
                });
        }
    });
    const auto socket = "ex-control-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_server.listen(socket)) { qCWarning(controlLog) << "无法启动桌面控制服务" << m_server.errorString(); return; }
    QSaveFile file(m_descriptor);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"socket", socket}, {"token", m_token}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly)) { m_server.close(); return; }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) { file.cancelWriting(); m_server.close(); return; }
    if (file.write(bytes) != bytes.size() || !file.commit()) m_server.close();
}
Host::~Host()
{
    if (m_server.isListening()) QFile::remove(m_descriptor);
}
QJsonObject Host::invoke(const QString &method, const QJsonObject &params, QString *error, int *code) const
{
    const auto found = m_methods.constFind(method);
    if (found == m_methods.cend()) {
        *error = QStringLiteral("未知桌面命令。"); if (code) *code = -32601; return {};
    }
    for (auto it = params.begin(); it != params.end(); ++it)
        if (!found->fields.contains(it.key())) {
            *error = QStringLiteral("命令包含未定义的字段。"); if (code) *code = -32602; return {};
        }
    if (code) *code = -32000;
    return found->handler(params, error);
}
void Host::add(const QString &method, QSet<QString> fields, Handler handler)
{
    m_methods.insert(method, {std::move(fields), std::move(handler)});
}
}
