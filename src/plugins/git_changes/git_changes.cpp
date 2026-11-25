#include "git_changes.h"
#include "untracked_preview.h"
#include <QElapsedTimer>
#include "git_read.h"
#include "revision_range.h"
#include <QtConcurrentRun>

namespace exagent {
namespace {
using namespace gitread;
QVariantMap snapshot(const QString &folder, const QString &key, const QString &requestedPath, const RevisionRange &range, const Cancel &cancel)
{
    QVariantList entries;
    QString selectedPath;
    int selectedArea = 0;
    const auto append = [&](const QString &path, char code, int area) {
        const auto entryKey = QString::number(area) + ":" + path;
        entries.append(QVariantMap{{"path", path}, {"code", QString(QChar::fromLatin1(code))},
                                   {"area", area}, {"key", entryKey}});
        // 文件树跳转优先展示磁盘改动，只有索引变更时自然回落到已暂存项。
        if (entryKey == key || (path == requestedPath && (!selectedArea || area == GitChanges::Worktree))) {
            selectedPath = path;
            selectedArea = area;
        }
    };
    QStringList revisions;
    bool limited = false;
    if (range.scope != GitChanges::Local) {
        const auto comparison = compareRevisions(folder, range, cancel);
        if (!comparison.error.isEmpty()) return {{"message", comparison.error}};
        revisions = comparison.revisions;
        limited = comparison.limited;
        for (const auto &change : comparison.files)
            append(change.path, change.code, change.untracked ? GitChanges::Untracked : GitChanges::Compared);
    } else {
        // porcelain -z 给出仓库相对路径；统一转换为当前工作区相对路径。
        auto prefix = runGit(folder, {"rev-parse", "--show-prefix"}, cancel, 16384);
        if (!prefix.error.isEmpty()) return {{"message", prefix.error}};
        if (prefix.data.endsWith('\n')) prefix.data.chop(1);
        auto status = runGit(folder, {"status", "--porcelain=v1", "-z", "--no-renames",
                                      "--untracked-files=all", "--ignore-submodules=none", "--", "."}, cancel, 1024*1024);
        if (!status.error.isEmpty()) return {{"message", status.error}};
        limited = status.truncated;
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
    }
    QVariantMap result{{"entries", entries}, {"selection", selectedPath.isEmpty() ? QString{} : QString::number(selectedArea) + ":" + selectedPath}};
    if (range.scope == GitChanges::Local && !cancel->load()) {
        const auto head = runGit(folder, {"symbolic-ref","--quiet","HEAD"}, cancel, 4096);
        if (head.error.isEmpty() && head.data.startsWith("refs/heads/"))
            result.insert("branch",QString::fromUtf8(head.data.trimmed().sliced(11)));
    }
    if (!requestedPath.isEmpty() && selectedPath.isEmpty()) result.insert("message", QStringLiteral("此文件在当前范围没有差异。"));
    if (limited) result.insert("message", QStringLiteral("变更清单仅显示可预览的前一部分。"));
    if (selectedPath.isEmpty() || cancel->load()) return result;
    QStringList args{"diff", "--no-ext-diff", "--no-textconv", "--no-color", "--no-renames", "--relative",
                     "--submodule=short"};
    if (selectedArea == GitChanges::Untracked) {
        result.insert(previewUntracked(folder, selectedPath));
        return result;
    }
    if (selectedArea == GitChanges::Index) args << "--cached";
    args << revisions << "--" << selectedPath;
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
        const auto branch = data.value("branch").toString();
        if (branch != m_branch) { m_branch = branch; emit branchChanged(); }
        const auto entries = data.value("entries").toList();
        if (m_entries != entries) { m_entries = entries; emit entriesChanged(); }
        const auto key = data.value("selection").toString();
        if (key != m_key) { m_key = key; emit selectionChanged(); }
        if (!key.isEmpty()) m_requestedPath.clear();
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
    m_branch.clear();
    emit branchChanged();
    m_requestedPath.clear();
    m_entries.clear();
    m_key.clear();
    emit folderChanged();
    emit entriesChanged();
    emit selectionChanged();
    invalidate();
}

void GitChanges::setRange(int scope, const QString &from, const QString &to)
{
    if (scope < Local || scope > Revisions) return;
    if (scope == m_scope && from == m_from && to == m_to) return;
    m_scope = scope;
    m_branch.clear();
    emit branchChanged();
    emit scopeChanged();
    m_from = from.trimmed();
    m_to = to.trimmed();
    m_requestedPath.clear();
    m_entries.clear();
    m_key.clear();
    emit entriesChanged();
    emit selectionChanged();
    invalidate();
}

void GitChanges::select(const QString &key)
{
    if (m_key == key) return;
    m_requestedPath.clear();
    m_key = key;
    emit selectionChanged();
    invalidate();
}

void GitChanges::selectFile(const QString &path)
{
    m_key.clear();
    m_requestedPath = path;
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
    m_job.setFuture(QtConcurrent::run(snapshot, m_folder, m_key, m_requestedPath, RevisionRange{m_scope, m_from, m_to}, m_cancel));
}
}
