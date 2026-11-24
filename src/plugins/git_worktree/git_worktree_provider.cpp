#include "git_worktree_provider.h"
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace exagent {
void GitWorktreeProvider::create(const QString &source, const QString &name, Completion done)
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worktrees";
    if (!QDir().mkpath(root)) {
        done({}, QStringLiteral("无法创建任务工作树存放目录：") + root);
        return;
    }
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto label = name.left(48);
    label.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), "-");
    const auto leaf = label + "-" + token.left(8);
    const auto path = QDir(root).filePath(leaf);
    auto *process = new QProcess(this);
    process->setWorkingDirectory(source);
    // 只基于来源工作区已提交的 HEAD 建分支，未提交内容保留在来源目录。
    connect(process, &QProcess::finished, process, [process, path, done](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0) done(path, {});
        else done({}, QStringLiteral("Git 创建任务工作树失败：") + QString::fromLocal8Bit(process->readAllStandardError()).trimmed());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        done({}, QStringLiteral("无法启动 Git：") + process->errorString());
        process->deleteLater();
    });
    process->start("git", {"worktree", "add", "-b", "feature/task-" + token, path, "HEAD"});
}
}
