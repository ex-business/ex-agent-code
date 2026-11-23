#pragma once
#include "codex_account.h"
#include <QJsonObject>
#include <QTimer>
namespace exagent::codex {
// app-server 通过独占子进程管道传递逐行 JSON；stderr 不进入聊天或协议日志。
class Connection final : public QObject {
    Q_OBJECT
public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;
    void start(Account account, const QString &cwd);
    bool send(const QJsonObject &frame);
signals:
    void ready();
    void frame(QJsonObject message);
    void failure(QString reason);

private:
    void readFrames();
    QProcess m_process;
    QByteArray m_buffer;
};
}
