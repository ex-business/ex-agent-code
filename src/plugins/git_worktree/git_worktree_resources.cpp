#include "git_worktree_provider.h"
#include <QDir>
#include <QFutureWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace exagent {
namespace {
struct Result { bool ok; QByteArray out; };
Result git(const QString &cwd, QStringList args)
{
    QProcess p;
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto &key : env.keys())
        if (key.startsWith("GIT_")) env.remove(key);
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(cwd);
    p.start("git", args);
    if (!p.waitForStarted() || !p.waitForFinished(-1)) return {false, p.errorString().toUtf8()};
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return {ok, ok ? p.readAllStandardOutput() : p.readAllStandardError()};
}

QVariantMap inspect(const QString &path, const QString &expectedBranch, bool remove)
{
    QVariantMap info{{"cleanable",false}};
    const auto fail = [&](const QString &reason) { info.insert("reason", reason); return info; };
    const QFileInfo dir(path);
    const QDir root(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worktrees");
    // 同时核对应用托管目录、Git 检出根与主工作树身份，普通绑定目录只允许查看。
    if (root.canonicalPath().isEmpty() || dir.absolutePath() != root.canonicalPath()
        || dir.canonicalFilePath() != path || !QFileInfo(path + "/.git").isFile())
        return fail(QStringLiteral("仅能管理此应用创建的任务工作树。"));
    const auto top = git(path, {"rev-parse","--show-toplevel"});
    const auto list = git(path, {"worktree","list","--porcelain","-z"});
    const auto ref = git(path, {"symbolic-ref","--quiet","--short","HEAD"});
    if (!top.ok || QFileInfo(QString::fromUtf8(top.out.trimmed())).canonicalFilePath() != path
        || !list.ok || !list.out.startsWith("worktree ") || !ref.ok)
        return fail(QStringLiteral("无法确认工作树或需保留的分支。"));
    const auto primary = QString::fromUtf8(list.out.split('\0').first().mid(9));
    if (QFileInfo(primary).canonicalFilePath() == path) return fail(QStringLiteral("主工作树不能清理。"));
    const auto branch = QString::fromUtf8(ref.out.trimmed());
    info.insert("branch", branch);
    info.insert("managed", true);
    const auto changes = git(path, {"status","--porcelain=v1","--ignored","--untracked-files=all","--ignore-submodules=none"});
    if (!changes.ok || !changes.out.isEmpty()) return fail(QStringLiteral("存在修改、未跟踪或忽略文件，或无法读取状态。"));
    if (remove) {
        if (expectedBranch != branch) return fail(QStringLiteral("分支已经变化，请重新检查。"));
        const auto result = git(primary, {"worktree","remove","--",path});
        if (!result.ok) return fail(QStringLiteral("Git 拒绝清理：") + QString::fromUtf8(result.out).trimmed());
    }
    info.insert("cleanable",true);
    return info;
}
}

void GitWorktreeProvider::resourceInfo(const QString &path, const QString &branch, bool remove, ResourceReady ready)
{
    auto *watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, watch, [watch, ready] {
        ready(watch->result());
        watch->deleteLater();
    });
    watch->setFuture(QtConcurrent::run(inspect, path, branch, remove));
}
}
