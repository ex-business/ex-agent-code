#pragma once
#include "application/chat/chat_controller.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalServer>
#include <QSet>
#include <functional>

namespace exagent::control {
class AutomationService;
// 命令按插件注册，传输层只负责本机认证、输入边界和结果封装。
class Host final : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<QJsonObject(const QJsonObject &, QString *)>;
    explicit Host(ChatController &chat, QObject *parent = nullptr);
    ~Host() override;
    void add(const QString &method, QSet<QString> fields, Handler handler);
    bool ready() const { return m_server.isListening(); }
    ChatController &chat;
private:
    struct Method { QSet<QString> fields; Handler handler; };
    QHash<QString, Method> m_methods;
    QLocalServer m_server;
    QString m_descriptor, m_token;
    int m_connections = 0;
};
QJsonObject paginate(const QJsonArray &items, const QJsonObject &params, QString *error);
void registerCore(Host &host);
void registerSkills(Host &host);
QString descriptorPath();
int runCli(QStringList arguments);
}
