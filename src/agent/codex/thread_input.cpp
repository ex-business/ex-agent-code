#include "thread_input.h"
#include "codex_connection.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <sys/file.h>
#include <sys/wait.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace exagent::codex {
std::unique_ptr<ThreadInputLock> claimThread(const QString& thread)
{
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/thread-input";
    if (thread.isEmpty() || !QDir().mkpath(dir)) return {};
    const auto key = QCryptographicHash::hash(thread.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto lock = std::make_unique<ThreadInputLock>(dir + '/' + key + ".lock");
#ifdef Q_OS_UNIX
    // 锁文件保留同一个 inode；崩溃时内核释放锁，避免错误回收活跃终端。
    if (!lock->open(QIODevice::ReadWrite) || flock(lock->handle(), LOCK_EX | LOCK_NB) != 0) return {};
#else
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) return {};
#endif
    return lock;
}
bool threadAvailable(const QString& thread) { return thread.isEmpty() || bool(claimThread(thread)); }
int terminalContinuation(const QVariantMap& spec)
{
    const auto thread = spec.value("threadId").toString();
    const auto cwd = spec.value("boundCwd").toString();
    const auto account = Account(spec.value("boundAccount").toInt());
    if (QUuid(thread).isNull() || !QFileInfo(cwd).isDir() || !QDir::setCurrent(cwd)
        || (account != System && account != Dedicated)) return 2;
    auto lock = claimThread(thread);
    if (!lock) { qWarning("此线程正在另一入口中使用。"); return 3; }
    // 终端服务可能早于宿主启动。固定原 CLI 与会话目录，并重新核对公开账号身份。
    const auto binary = spec.value("program").toString();
    if (!QFileInfo(binary).isExecutable() || !QFileInfo(binary).isAbsolute()) return 2;
    qputenv("PATH", QFileInfo(binary).absolutePath().toUtf8() + ':' + qgetenv("PATH"));
    if (account == System) qputenv("CODEX_HOME", spec.value("home").toString().toUtf8());
    bool matched = false;
    {
        Connection connection;
        QEventLoop loop;
        QObject::connect(&connection, &Connection::ready, &loop, [&] {
            connection.send({{"id",1},{"method","initialize"},{"params",QJsonObject{{"clientInfo",QJsonObject{{"name","ex_terminal"},{"version","1"}}}}}});
        });
        QObject::connect(&connection, &Connection::frame, &loop, [&](const QJsonObject& event) {
            const auto id = event["id"].toInt();
            if (id == 1 && event.contains("result")) {
                connection.send({{"method","initialized"}});
                connection.send({{"id",2},{"method","account/read"},{"params",QJsonObject{{"refreshToken",false}}}});
            } else if (id == 2) {
                const auto owner = identity(account, event["result"].toObject()["account"].toObject());
                matched = event.contains("result") && !owner.isEmpty() && owner == spec.value("identity").toString()
                    && (account != Dedicated || event["result"].toObject()["account"].toObject()["type"] == "apiKey");
                loop.quit();
            } else if (event.contains("error")) loop.quit();
        });
        QObject::connect(&connection, &Connection::failure, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        connection.start(account,cwd);
        loop.exec();
    }
    if (!matched) { qWarning("账号身份无法核对，已取消终端续接。"); return 4; }
    QProcess cli;
    QString error;
    if (!prepare(cli,account,&error)) return 5;
    const bool full = spec.value("fullAccess").toBool();
    auto args = accountArguments(account);
    args << "-a" << (full ? "never" : "on-request") << "-s"
         << (full ? "danger-full-access" : spec.value("write").toBool() ? "workspace-write" : "read-only");
    if (!spec.value("model").toString().isEmpty()) args << "-m" << spec.value("model").toString();
    args << "resume" << thread;
    cli.setProgram(binary);
#ifdef Q_OS_UNIX
    // 父子进程共用控制终端与锁；即使 CLI 主动关闭继承的描述符，父进程仍持锁至退出。
    QList<QByteArray> av {QFile::encodeName(binary)}, ev;
    for (const auto& arg : args) av.append(arg.toUtf8());
    for (const auto& entry : cli.processEnvironment().toStringList()) ev.append(entry.toUtf8());
    QList<char*> argv, envp;
    for (auto& arg : av) argv.append(arg.data());
    for (auto& entry : ev) envp.append(entry.data());
    argv.append(nullptr);
    envp.append(nullptr);
    if (fcntl(lock->handle(), F_SETFD, 0) < 0) return 6;
    auto* executablePath = argv.front();
    auto** argumentData = argv.data();
    auto** environmentData = envp.data();
    const auto pid = fork();
    if (pid < 0) return 6;
    if (pid == 0) {
        execve(executablePath, argumentData, environmentData);
        _exit(126);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 6;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 7;
#else
    cli.setStandardErrorFile({});
    cli.setInputChannelMode(QProcess::ForwardedInputChannel);
    cli.setProcessChannelMode(QProcess::ForwardedChannels);
    cli.setArguments(args);
    cli.start();
    if (!cli.waitForStarted() || !cli.waitForFinished(-1)) return 6;
    return cli.exitStatus() == QProcess::NormalExit ? cli.exitCode() : 7;
#endif
}
}
