#include "codex_evidence.h"
#include "patch_lines.h"
#include <QDir>
#include <QSet>

namespace exagent::review {
QJsonObject codexEvidence(const QString &workspace, const QJsonArray &changes)
{
    // 仅接收已完成的结构化修改；超限、重复路径与移动文件均不推断来源。
    const QDir cwd(workspace);
    QJsonObject proofs;
    QSet<QString> seen;
    qsizetype budget = 256 * 1024;
    for (const auto &entry : changes) {
        const auto change = entry.toObject();
        const auto rawPath = change["path"].toString();
        const auto path = QDir::cleanPath(cwd.relativeFilePath(cwd.absoluteFilePath(rawPath)));
        const auto body = change["diff"].toString();
        budget -= body.size() + path.size();
        if (budget < 0 || seen.size() >= 64) { proofs = {}; break; }
        if (seen.contains(path)) { proofs.remove(path); continue; }
        seen.insert(path);
        if (path == "." || path == ".." || path.startsWith("../") || QDir::isAbsolutePath(path)) continue;
        auto lines = body.split('\n');
        const auto kind = change["kind"].toObject();
        if (kind["type"] == "add" || kind["type"] == "delete") {
            const bool add = kind["type"] == "add";
            const bool newline = body.endsWith('\n');
            if (newline) lines.removeLast();
            const auto count = lines.size();
            for (auto &line : lines) line.prepend(add ? '+' : '-');
            lines.prepend(add ? QString("@@ -0,0 +1,%1 @@").arg(count)
                              : QString("@@ -1,%1 +0,0 @@").arg(count));
            if (!newline) lines << "\\ No newline at end of file";
        } else if (kind["type"] != "update" || kind["move_path"].isString()
                   || kind["movePath"].isString()) continue;
        const auto sig = review::parsePatch(lines).signature;
        if (!sig.isEmpty()) proofs.insert(path,sig);
    }
    return proofs;
}
}
