#include "codex_connection.h"
#include <QJsonDocument>
namespace exagent::codex {
Connection::Connection(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::started, this, &Connection::ready);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Connection::readFrames);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failure(QStringLiteral("无法启动 Codex CLI。"));
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        readFrames();
        emit failure(QStringLiteral("Codex 进程已退出，当前运行结果需核对。"));
    });
}
Connection::~Connection()
{
    m_process.disconnect(this);
    m_process.closeWriteChannel();
    if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(300)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
void Connection::start(Account account, const QString &cwd, const QString &home)
{
    QString error;
    if (!prepare(m_process, account, &error, home)) {
        emit failure(error);
        return;
    }
    m_process.setWorkingDirectory(cwd);
    m_process.setArguments(accountArguments(account) + QStringList { "app-server", "--listen", "stdio://" });
    if(!home.isEmpty())m_process.setArguments(QStringList{"-c",R"(cli_auth_credentials_store="file")"}+m_process.arguments());
    m_process.start();
}
bool Connection::send(const QJsonObject &frame)
{
    const auto data = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
    return m_process.state() == QProcess::Running && m_process.write(data) == data.size();
}
void Connection::readFrames()
{
    // 限制单行大小，同时允许一次系统读取含有多条完整通知。
    constexpr int limit = 16 * 1024 * 1024;
    while (m_process.bytesAvailable()) {
        m_buffer += m_process.read(65536);
        qsizetype pos;
        while ((pos = m_buffer.indexOf('\n')) != -1) {
            const auto frameData = m_buffer.left(pos).trimmed();
            m_buffer.remove(0, pos + 1);
            if (frameData.isEmpty())
                continue;
            const auto doc = QJsonDocument::fromJson(frameData);
            if (pos > limit || !doc.isObject()) {
                emit failure(QStringLiteral("Codex 协议帧无效。"));
                return;
            }
            emit frame(doc.object());
        }
        if (m_buffer.size() > limit) {
            emit failure(QStringLiteral("Codex 单条协议帧超过容量。"));
            return;
        }
    }
}
}
