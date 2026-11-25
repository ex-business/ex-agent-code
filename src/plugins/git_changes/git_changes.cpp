#include "git_changes.h"
#include "untracked_preview.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QtConcurrentRun>

namespace exagent {
namespace {
using Cancel = std::shared_ptr<std::atomic_bool>;
struct Output {
    QByteArray data;
    QString error;
    bool truncated = false;
};

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

QVariantMap snapshot(const QString &folder, const QString &key, const Cancel &cancel)
{
    // porcelain -z 始终给出仓库相对路径。子目录工作区只显示其边界内的变更。
    auto prefix = runGit(folder, {"rev-parse", "--show-prefix"}, cancel, 16384);
    if (!prefix.error.isEmpty()) return {{"message", prefix.error}};
    if (prefix.data.endsWith('\n')) prefix.data.chop(1);
    auto status = runGit(folder, {"status", "--porcelain=v1", "-z", "--no-renames",
                                  "--untracked-files=all", "--ignore-submodules=none", "--", "."}, cancel, 1024*1024);
    if (!status.error.isEmpty()) return {{"message", status.error}};
    QVariantList entries;
    QString selectedPath;
    int selectedArea = 0;
    const auto append = [&](const QString &path, char code, int area) {
        const auto entryKey = QString::number(area) + ":" + path;
        entries.append(QVariantMap{{"path", path}, {"code", QString(QChar::fromLatin1(code))},
                                   {"area", area}, {"key", entryKey}});
        if (entryKey == key) { selectedPath = path; selectedArea = area; }
    };
    bool limited = status.truncated;
    for (qsizetype offset = 0; offset < status.data.size();) {
        const auto end = status.data.indexOf('\0', offset);
        if (end < 0) break;
        const auto record = status.data.sliced(offset, end-offset);
        offset = end + 1;
        if (record.size() < 4 || !record.sliced(3).startsWith(prefix.data)) continue;
        const auto bytes = record.sliced(3 + prefix.data.size());
        const auto path = QString::fromUtf8(bytes);
        // 无法用 QString 往返的文件名不可作为另一个同名路径传给 Git。
        if (path.toUtf8() != bytes) { limited = true; continue; }
        const auto xy = record.first(2);
        if (xy == "??") append(path, '?', GitChanges::Untracked);
        else if (xy.contains('U') || xy == "AA" || xy == "DD") append(path, 'U', GitChanges::Conflict);
        else {
            if (xy[0] != ' ') append(path, xy[0], GitChanges::Index);
            if (xy[1] != ' ') append(path, xy[1], GitChanges::Worktree);
        }
        if (entries.size() >= 3000 && offset < status.data.size()) { limited = true; break; }
    }
    QVariantMap result{{"entries", entries}, {"selection", selectedPath.isEmpty() ? QString{} : key}};
    if (limited) result.insert("message", QStringLiteral("变更清单仅显示可预览的前一部分。"));
    if (selectedPath.isEmpty() || cancel->load()) return result;
    QStringList args{"diff", "--no-ext-diff", "--no-textconv", "--no-color", "--no-renames", "--relative",
                     "--submodule=short"};
    if (selectedArea == GitChanges::Untracked) {
        result.insert(previewUntracked(folder, selectedPath));
        return result;
    }
    if (selectedArea == GitChanges::Index) args << "--cached";
    args << "--" << selectedPath;
    QElapsedTimer diffTime;
    diffTime.start();
    auto patch = runGit(folder, args, cancel, 128*1024);
    result.insert("slow", patch.truncated || diffTime.elapsed() > 500);
    if (!patch.error.isEmpty()) result.insert("message", patch.error);
    else {
        auto lines = QString::fromUtf8(patch.data).split('\n');
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        if (patch.truncated || lines.size() > 3000) {
            lines = lines.mid(0, 3000);
            lines.append(QStringLiteral("…"));
            result.insert("message", QStringLiteral("差异过长，仅显示开头部分。"));
        }
        if (lines.isEmpty()) result.insert("message", QStringLiteral("此条目没有可显示的差异。"));
        result.insert("lines", lines);
    }
    return result;
}
}

GitChanges::GitChanges(QObject *parent) : QObject(parent)
{
    m_poll.setSingleShot(true);
    m_poll.setInterval(2500);
    connect(&m_poll, &QTimer::timeout, this, &GitChanges::refresh);
    connect(&m_job, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const bool obsolete = m_cancel->load();
        m_cancel.reset();
        if (obsolete) { refresh(); return; }
        const auto data = m_job.result();
        const auto entries = data.value("entries").toList();
        if (m_entries != entries) { m_entries = entries; emit entriesChanged(); }
        const auto key = data.value("selection").toString();
        if (key != m_key) { m_key = key; emit selectionChanged(); }
        publishLines(data.value("lines").toStringList());
        publishMessage(data.value("message").toString());
        // 超限或慢查询降低自动轮询频率，用户仍可随时手动刷新。
        if (!m_folder.isEmpty()) m_poll.start(data.value("slow").toBool() ? 15000 : 2500);
    });
}

GitChanges::~GitChanges() { if (m_cancel) m_cancel->store(true); }

void GitChanges::publishLines(const QStringList &lines)
{
    if (m_lines != lines) { m_lines = lines; emit linesChanged(); }
}

void GitChanges::publishMessage(const QString &message)
{
    if (m_message != message) { m_message = message; emit messageChanged(); }
}

void GitChanges::setFolder(const QString &folder)
{
    if (m_folder == folder) return;
    m_folder = folder;
    m_entries.clear();
    m_key.clear();
    emit folderChanged();
    emit entriesChanged();
    emit selectionChanged();
    invalidate();
}

void GitChanges::select(const QString &key)
{
    if (m_key == key) return;
    m_key = key;
    emit selectionChanged();
    invalidate();
}

void GitChanges::invalidate()
{
    m_poll.stop();
    publishLines({});
    publishMessage(m_folder.isEmpty() ? QString{} : QStringLiteral("正在加载…"));
    if (m_cancel) m_cancel->store(true);
    else refresh();
}

void GitChanges::refresh()
{
    if (m_folder.isEmpty() || m_cancel) return;
    m_poll.stop();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_job.setFuture(QtConcurrent::run(snapshot, m_folder, m_key, m_cancel));
}
}
