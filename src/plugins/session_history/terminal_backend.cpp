#include "terminal_backend.h"
#include "history_provider.h"
#include <QCryptographicHash>
#include <QProcess>
#include <QStandardPaths>
namespace exagent {
TerminalResult terminalCommand(QStringList args)
{
    const auto exe = cliPath("tmux");
    if (exe.isEmpty())
        return { false, QStringLiteral("未找到 tmux。").toUtf8() };
    // 使用独立服务；IDE 的关闭与重开不会结束终端，亦不会连接用户默认 tmux 服务。
    const auto scope = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto socket
        = "exagent-" + QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    QProcess p;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove("TMUX");
    env.insert("PATH", env.value("PATH") + ":/opt/homebrew/bin:/usr/local/bin");
    p.setProcessEnvironment(env);
    p.start(exe, QStringList { "-L", socket, "-f", "/dev/null" } + args);
    if (!p.waitForStarted() || !p.waitForFinished(5000))
        return { false, p.errorString().toUtf8() };
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return { ok, ok ? p.readAllStandardOutput() : p.readAllStandardError() };
}
QVariantMap terminalSnapshot(const QString& id)
{
    const auto target = "=" + id + ":0.0";
    const auto meta
        = terminalCommand({ "display-message", "-p", "-t", target, "#{session_name} #{pane_pid} #{pane_dead}" });
    const auto parts = meta.output.trimmed().split(' ');
    if (!meta.ok || parts.size() != 3 || parts[0] != id.toUtf8())
        return { { "state", 0 } };
    const auto capture = terminalCommand({ "capture-pane", "-p", "-t", target, "-S", "-1500" });
    QVariantMap result { { "state", parts[2] == "0" ? 1 : 0 }, { "pid", parts[1].toLongLong() } };
    if (capture.ok)
        result.insert("text", QString::fromUtf8(capture.output).right(512 * 1024));
    return result;
}
}
