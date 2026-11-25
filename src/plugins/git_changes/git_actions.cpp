#include "git_actions.h"
#include "git_read.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QtConcurrentRun>

namespace exagent {
namespace {
QVariantMap failure(const QString &notice, const QString &detail = {})
{
    return {{"notice", notice}, {"detail", detail}, {"ok", false}};
}

QString diagnostic(QByteArray bytes)
{
    QString text = QString::fromUtf8(bytes).trimmed();
    // 远端与钩子输出仅在本地操作面板展示，屏蔽 URL 中的认证信息。
    static const QRegularExpression credentials(R"((\w+://)[^\s/@]+@)");
    static const QRegularExpression query(R"((\w+://[^\s?]+)\?[^\s']+)");
    text.replace(credentials, "\\1").replace(query, "\\1");
    return text;
}

// 写操作使用独立进程，保留仓库钩子。切换面板只隔离结果，不中止进行中的提交或推送。
QVariantMap execute(const QString &folder, const QStringList &args, const QString &done,
                    const QByteArray &input = {})
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.setProcessEnvironment(gitread::workspaceEnvironment());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("git", QStringList{"--no-pager", "--literal-pathspecs"} + args);
    if (!process.waitForStarted(3000)) return failure(QStringLiteral("无法启动 Git。"));
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    QByteArray tail;
    do {
        process.waitForFinished(80);
        tail = (tail + process.readAll()).right(8192);
        if (process.state() != QProcess::NotRunning && timer.elapsed() > 120000) {
            // 先让 Git 执行自身的退出清理，避免立即强杀留下索引锁。
            process.terminate();
            if (!process.waitForFinished(3000)) process.kill();
            process.waitForFinished();
            return failure(QStringLiteral("操作超时，请核对仓库状态。"), diagnostic(tail));
        }
    } while (process.state() != QProcess::NotRunning);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return failure(QStringLiteral("Git 操作失败。"), diagnostic(tail));
    return {{"ok",true}, {"notice",done}, {"detail",diagnostic(tail)}};
}

QVariantMap indexReview(const QString &folder, const gitread::Cancel &cancel)
{
    // 全仓库原始差异包含路径、模式及新旧对象 ID。即使文件名相同，重新暂存也会使审阅失效。
    const auto raw = gitread::runGit(folder, {"diff","--cached","--raw","--no-abbrev","--no-renames",
        "--no-relative","--no-ext-diff","--no-textconv","--ignore-submodules=none","-z","--"}, cancel, 2*1024*1024);
    if (!raw.error.isEmpty()) return failure(raw.error);
    if (raw.truncated) return failure(QStringLiteral("暂存区过大，无法完整审阅。"));
    QStringList files;
    qsizetype pos = 0;
    while (pos < raw.data.size()) {
        const auto headerEnd = raw.data.indexOf('\0', pos);
        const auto pathEnd = raw.data.indexOf('\0', headerEnd + 1);
        if (headerEnd < pos || pathEnd <= headerEnd) return failure(QStringLiteral("暂存区记录无效。"));
        const auto header = raw.data.sliced(pos, headerEnd-pos);
        if (header.endsWith(" U")) return failure(QStringLiteral("请先解决合并冲突。"));
        const auto bytes = raw.data.sliced(headerEnd+1, pathEnd-headerEnd-1);
        auto name = QString::fromUtf8(bytes);
        if (name.toUtf8() != bytes) return failure(QStringLiteral("暂存区包含无法显示的文件名。"));
        files << QString::fromLatin1(header.right(1)) + "  " + name.replace("\\","\\\\").replace("\n","\\n").replace("\r","\\r").replace("\t","\\t");
        if (files.size() > 3000) return failure(QStringLiteral("暂存区过大，无法完整审阅。"));
        pos = pathEnd + 1;
    }
    if (files.isEmpty()) return failure(QStringLiteral("没有已暂存的变更。"));
    return {{"ok",true},{"files",files},{"snapshot",QCryptographicHash::hash(raw.data,QCryptographicHash::Sha256)}};
}

QVariantMap perform(const QString &folder, GitActions::Operation op, const QString &branch,
                    const QString &value, const QByteArray &snapshot)
{
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    auto head = gitread::runGit(folder, {"symbolic-ref","--quiet","HEAD"}, cancel, 4096);
    if (branch.isEmpty() || !head.error.isEmpty() || QString::fromUtf8(head.data).trimmed() != "refs/heads/" + branch)
        return failure(QStringLiteral("当前分支已变更或处于分离状态，请刷新。"));
    if (op == GitActions::Review || op == GitActions::Commit) {
        auto review = indexReview(folder, cancel);
        if (op == GitActions::Review || !review.value("ok").toBool()) return review;
        if (review.value("snapshot").toByteArray() != snapshot)
            return failure(QStringLiteral("暂存内容已变化，请重新审阅后提交。"));
        return execute(folder, {"commit","-F","-"}, QStringLiteral("提交成功。"), value.toUtf8());
    }
    if (op == GitActions::Push) {
        const auto remote = gitread::runGit(folder, {"remote"}, cancel, 32768);
        if (!remote.error.isEmpty() || !remote.data.split('\n').contains("origin"))
            return failure(QStringLiteral("仓库尚未配置 origin 远端。"));
        const QString ref = "refs/heads/" + branch;
        // 明确限定一个分支；配置中的镜像、附带标签或默认 refspec 不扩大推送范围。
        QStringList args{"-c","remote.origin.mirror=false","-c","push.followTags=false","push","--porcelain","--recurse-submodules=no"};
        const auto upstream = gitread::runGit(folder, {"for-each-ref","--format=%(upstream)",ref}, cancel, 4096);
        if (!upstream.error.isEmpty()) return failure(upstream.error);
        if (upstream.data.trimmed().isEmpty()) args << "--set-upstream";
        args << "--" << "origin" << (ref + ":" + ref);
        return execute(folder, args, QStringLiteral("推送成功。"));
    }
    const auto parts = value.split('/');
    if (QDir::isAbsolutePath(value) || parts.contains("") || parts.contains(".") || parts.contains("..") ||
        parts.contains(".git", Qt::CaseInsensitive) || value.contains(QChar::Null))
        return failure(QStringLiteral("只能操作工作区内的单个文件。"));
    const QFileInfo file(QDir(folder).filePath(value));
    if (file.isDir() && !file.isSymLink()) return failure(QStringLiteral("只能操作工作区内的单个文件。"));
    // update-index 按单个索引项工作，避免文件被换成目录时 pathspec 递归纳入其他文件。
    const auto bytes = value.toUtf8();
    if (op == GitActions::Unstage) {
        const auto raw = gitread::runGit(folder, {"diff","--cached","--raw","--no-abbrev","--no-renames",
            "--relative","--no-ext-diff","--no-textconv","--ignore-submodules=none","-z","--",value}, cancel, 16384);
        if (!raw.error.isEmpty()) return failure(raw.error);
        const auto records = raw.data.split('\0');
        if (raw.truncated || records.size() != 3 || records[1] != bytes)
            return failure(QStringLiteral("文件状态已变化，请刷新后重试。"));
        const auto fields = records[0].split(' ');
        if (fields.size() != 5 || !fields[0].startsWith(':') || fields[4] == "U")
            return failure(QStringLiteral("暂存区记录无效。"));
        const auto oldMode = fields[0].mid(1);
        if (oldMode == "000000")
            return execute(folder, {"update-index","--force-remove","--",value}, QStringLiteral("已撤销暂存。"));
        // --cacheinfo 的路径总是相对仓库根，而普通 update-index 路径相对当前目录。
        auto prefix = gitread::runGit(folder, {"rev-parse","--show-prefix"}, cancel, 16384);
        if (!prefix.error.isEmpty() || prefix.truncated || !prefix.data.endsWith('\n'))
            return failure(QStringLiteral("暂存区记录无效。"));
        prefix.data.chop(1);
        const auto repoPath = QString::fromUtf8(prefix.data) + value;
        return execute(folder, {"update-index","--add","--cacheinfo",QString::fromLatin1(oldMode),
                                 QString::fromLatin1(fields[2]),repoPath}, QStringLiteral("已撤销暂存。"));
    }
    const auto eligible = gitread::runGit(folder, {"ls-files","--cached","--others","--exclude-standard",
        "--deduplicate","-z","--",value}, cancel, 16384);
    if (!eligible.error.isEmpty()) return failure(eligible.error);
    if (eligible.truncated || eligible.data != bytes + '\0')
        return failure(QStringLiteral("文件状态已变化，请刷新后重试。"));
    return execute(folder, {"update-index","--add","--remove","--",value}, QStringLiteral("文件已暂存。"));
}
}

GitActions::GitActions(QObject *parent) : QObject(parent)
{
    connect(&m_job, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_job.result();
        m_busy = false;
        if (m_context != m_started) { emit changed(); emit refreshed(); return; }
        const bool ok = result.value("ok").toBool();
        m_state = ok ? Succeeded : Failed;
        m_notice = result.value("notice").toString();
        m_detail = result.value("detail").toString();
        if (ok && m_operation == Review) {
            m_staged = result.value("files").toStringList();
            m_snapshot = result.value("snapshot").toByteArray();
        }
        emit changed();
        if (ok && m_operation == Review) emit reviewReady();
        if (ok && m_operation == Commit) emit committed();
        if (m_operation != Review) emit refreshed();
    });
}

void GitActions::setFolder(const QString &folder)
{
    if (folder == m_folder) return;
    ++m_context;
    m_folder = folder;
    m_snapshot.clear();
    m_staged.clear();
    m_state = Idle;
    m_notice.clear();
    m_detail.clear();
    emit changed();
}

void GitActions::start(Operation op, const QString &branch, const QString &value)
{
    if (m_busy || m_folder.isEmpty() || branch.isEmpty()) return;
    m_operation = op;
    m_started = m_context;
    m_branch = branch;
    m_state = Running;
    m_busy = true;
    m_notice = QStringLiteral("正在执行 Git 操作…");
    m_detail.clear();
    m_job.setFuture(QtConcurrent::run(perform, m_folder, op, branch, value, m_snapshot));
    emit changed();
}

void GitActions::stageFile(const QString &branch, const QString &path, bool unstage)
{
    start(unstage ? Unstage : Stage, branch, path);
}
void GitActions::reviewCommit(const QString &branch) { start(Review, branch); }
void GitActions::commit(const QString &message)
{
    if (!m_snapshot.isEmpty() && !message.trimmed().isEmpty()) start(Commit, m_branch, message);
}
void GitActions::push(const QString &branch) { start(Push, branch); }
}
