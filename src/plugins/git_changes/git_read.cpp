#include "git_read.h"
#include <QElapsedTimer>
#include <QProcess>

namespace exagent::gitread {
// Git 只读进程在工作线程运行，切换工作区或隐藏面板时停止旧请求。
Output runGit(const QString &folder, QStringList args, const Cancel &cancel,
              int limit)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto key : {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE",
                           "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES"})
        env.remove(key);
    env.insert("GIT_NO_LAZY_FETCH", "1");
    env.insert("LC_ALL", "C");
    env.insert("GIT_TERMINAL_PROMPT", "0");
    process.setProcessEnvironment(env);
    process.setStandardInputFile(QProcess::nullDevice());
    QStringList options{"--no-pager", "--no-optional-locks", "--literal-pathspecs",
                        "-c", "core.quotepath=false", "-c", "core.bigFileThreshold=8m",
                        "-c", "core.fsmonitor=false", "-c", "core.untrackedCache=false"};
    process.start("git", options + args, QIODevice::ReadOnly);
    Output out;
    if (!process.waitForStarted(2000)) {
        out.error = QStringLiteral("无法启动 Git。");
        return out;
    }
    QByteArray diagnostic;
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        process.waitForFinished(40);
        auto chunk = process.readAllStandardOutput();
        const auto remaining = limit - out.data.size();
        out.truncated = chunk.size() > remaining;
        out.data.append(chunk.constData(), qMin(remaining, chunk.size()));
        diagnostic += process.readAllStandardError().left(2048 - diagnostic.size());
        if (cancel->load() || out.truncated || elapsed.elapsed() > 10000) {
            if (!cancel->load() && !out.truncated) out.error = QStringLiteral("读取 Git 状态超时。");
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished();
            }
            return out;
        }
    } while (process.state() != QProcess::NotRunning);
    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        out.error = diagnostic.contains("not a git repository")
            ? QStringLiteral("当前工作区不在 Git 仓库中。") : QStringLiteral("Git 读取失败。");
        if (!diagnostic.isEmpty() && !diagnostic.contains("not a git repository"))
            out.error += "\n" + QString::fromUtf8(diagnostic).trimmed();
    }
    return out;
}

}
