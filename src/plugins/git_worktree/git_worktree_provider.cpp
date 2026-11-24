#include "git_worktree_provider.h"
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace exagent {
namespace {
QProcess *gitProcess(QObject *parent, const QString &source)
{
    auto *process = new QProcess(parent);
    auto env = QProcessEnvironment::systemEnvironment();
    // 工作区以会话绑定目录为准，避免从启动 IDE 的终端继承其他仓库的位置。
    for (const auto key : {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE"})
        env.remove(key);
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(source);
    return process;
}
}

void GitWorktreeProvider::loadFields(const QString &source, FieldsReady ready)
{
    auto *process = gitProcess(this, source);
    const auto publish = [process, ready](QStringList refs) {
        refs.prepend("HEAD");
        ready({QVariantMap{{"key","start"}, {"label","起点（分支、标签或提交）"}, {"choices",refs}},
               QVariantMap{{"key","branch"}, {"label","新分支名（留空自动生成）"}}});
        process->deleteLater();
    };
    connect(process, &QProcess::finished, process, [process, publish](int code, QProcess::ExitStatus status) {
        QStringList refs;
        if (code == 0 && status == QProcess::NormalExit)
            refs = QString::fromUtf8(process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        publish(refs);
    });
    connect(process, &QProcess::errorOccurred, process, [publish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) publish({});
    });
    process->start("git", {"for-each-ref", "--format=%(refname:short)", "refs/heads", "refs/remotes", "refs/tags"});
}

void GitWorktreeProvider::create(const QString &source, const QString &name, const QVariantMap &options, Completion done)
{
    auto start = options.value("start").toString().trimmed();
    if (start.isEmpty()) start = "HEAD";
    auto branch = options.value("branch").toString().trimmed();
    if (branch.startsWith("code", Qt::CaseInsensitive) || branch.startsWith('-') || start.startsWith('-')
        || start.contains(QChar::Null) || branch.contains(QChar::Null)) {
        done({}, QStringLiteral("请检查起点或分支名；新分支不能以 code 或短横线开头。"));
        return;
    }
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
    auto *process = gitProcess(this, source);
    // 使用 -b 保证同名分支不会被覆盖，交给 Git 原子检查引用及起点。
    if (branch.isEmpty()) branch = "feature/task-" + token;
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
    process->start("git", {"worktree", "add", "-b", branch, "--", path, start});
}
}
