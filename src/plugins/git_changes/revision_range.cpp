#include "revision_range.h"
#include "git_changes.h"

namespace exagent {
namespace {
QString resolveCommit(const QString &folder, const QString &ref, const gitread::Cancel &cancel)
{
    if (ref.isEmpty() || ref.size() > 1024 || ref.startsWith('-') || ref.contains(QChar::Null)) return {};
    // 每次快照先固定提交，再用同一对象读取清单和补丁，避免分支移动造成两者错配。
    const auto result = gitread::runGit(folder, {"rev-parse", "--verify", "--end-of-options",
                                                  ref + "^{commit}"}, cancel, 256);
    const auto hash = result.data.trimmed();
    if (!result.error.isEmpty() || (hash.size() != 40 && hash.size() != 64)) return {};
    for (char ch : hash) if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')) return {};
    return QString::fromLatin1(hash);
}

void appendPaths(Comparison &result, const gitread::Output &output, bool untracked)
{
    result.limited |= output.truncated;
    result.error = output.error;
    // --name-status -z 的状态和路径各占一项；ls-files -z 只给出路径。
    for (qsizetype pos = 0; pos < output.data.size();) {
        if (result.files.size() >= 3000) { result.limited = true; return; }
        char code = '?';
        if (!untracked) {
            const auto end = output.data.indexOf('\0', pos);
            if (end <= pos) break;
            code = output.data[pos];
            pos = end + 1;
        }
        const auto end = output.data.indexOf('\0', pos);
        if (end < 0) break;
        const auto bytes = output.data.sliced(pos, end - pos);
        pos = end + 1;
        const auto path = QString::fromUtf8(bytes);
        if (bytes.isEmpty() || path.toUtf8() != bytes) { result.limited = true; continue; }
        result.files.append({path, code, untracked});
    }
}
}

Comparison compareRevisions(const QString &folder, const RevisionRange &range, const gitread::Cancel &cancel)
{
    Comparison result;
    const QStringList refs = range.scope == GitChanges::Revisions ? QStringList{range.from, range.to}
                                                                  : QStringList{range.from};
    for (const auto &ref : refs) {
        if (cancel->load()) return result;
        const auto commit = resolveCommit(folder, ref, cancel);
        if (commit.isEmpty()) {
            result.error = QStringLiteral("无法解析比较版本。") + "\n" + ref;
            return result;
        }
        result.revisions << commit;
    }
    QStringList args{"diff", "--name-status", "-z", "--relative", "--no-renames", "--no-ext-diff",
                     "--no-textconv", "--ignore-submodules=none"};
    args << result.revisions << "--" << ".";
    appendPaths(result, gitread::runGit(folder, args, cancel, 1024*1024), false);
    if (range.scope == GitChanges::RevisionToWorktree && result.error.isEmpty() && !cancel->load()) {
        const auto others = gitread::runGit(folder, {"ls-files", "--others", "--exclude-standard", "-z", "--", "."},
                                             cancel, 1024*1024);
        appendPaths(result, others, true);
    }
    return result;
}
}
