#include "git_worktree_provider.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace exagent {
namespace {
enum class SetupPolicy { Skip = 0, Run = 1 };

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

void prepareWorkspace(QObject *owner, const QString &source, const QString &path, bool runSetup,
                      TaskWorkspaceProvider::Completion done)
{
    QFile script(":/worktree-setup/prepare_worktree.rb");
    if (!script.open(QIODevice::ReadOnly)) {
        done({}, QStringLiteral("缺少工作树准备组件，工作树已保留：") + path);
        return;
    }
    auto *process = gitProcess(owner, source);
    // 后台准备成功后才发布会话，失败时保留工作树、分支及日志。
    QObject::connect(process, &QProcess::finished, process, [process, path, done](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0) done(path, {});
        else done({}, QString::fromUtf8(process->readAllStandardError()).trimmed()
                          + QStringLiteral("\n已保留工作树：") + path);
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process, path, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done({}, QStringLiteral("无法启动 Ruby 工作树准备组件，已保留工作树：") + path);
            process->deleteLater();
        }
    });
    process->start("/usr/bin/ruby", {"--disable-gems", "-W0", "-e", QString::fromUtf8(script.readAll()),
                                     "--", source, path, runSetup ? "run" : "skip"});
}

}

void GitWorktreeProvider::loadFields(const QString &source, FieldsReady ready)
{
    auto *process = gitProcess(this, source);
    const auto publish = [process, ready](QStringList refs) {
        refs.prepend("HEAD");
        ready({QVariantMap{{"key","start"}, {"label","起点（分支、标签或提交）"}, {"choices",refs}},
               QVariantMap{{"key","branch"}, {"label","新分支名（留空自动生成）"}},
               QVariantMap{{"key","setup"}, {"label","目标版本 orca.yaml 中的 setup 脚本"},
                           {"choices",QStringList{"跳过", "运行仓库脚本"}},
                           {"values",QVariantList{int(SetupPolicy::Skip), int(SetupPolicy::Run)}}}});
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
    connect(process, &QProcess::finished, process, [this, process, source, path, runSetup = options.value("setup").toInt() == int(SetupPolicy::Run), done](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0) prepareWorkspace(this, source, path, runSetup, done);
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
