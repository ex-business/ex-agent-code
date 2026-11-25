#include "patch_lines.h"
#include <QCryptographicHash>
#include <QRegularExpression>

namespace exagent::review {
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

PatchLines parsePatch(const QStringList &lines)
{
    static const QRegularExpression header(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(?: .*)?$)");
    PatchLines patch;
    QByteArray changes;
    int oldLine = 0, newLine = 0, oldLeft = 0, newLeft = 0;
    bool seenHunk = false;
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        if (line.startsWith("@@")) {
            const auto m = header.match(line);
            if (oldLeft || newLeft || !m.hasMatch()) return {};
            auto number = [&](int group, int fallback) {
                if (!m.hasCaptured(group)) return fallback;
                bool ok = false;
                const auto n = m.capturedView(group).toInt(&ok);
                return ok && n <= INT_MAX - lines.size() ? n : -1;
            };
            oldLine = number(1,0); oldLeft = number(2,1);
            newLine = number(3,0); newLeft = number(4,1);
            if (oldLine < 0 || oldLeft < 0 || newLine < 0 || newLeft < 0
                || (oldLeft && !oldLine) || (newLeft && !newLine)) return {};
            seenHunk = true;
            continue;
        }
        if (!seenHunk) continue;
        if (line == "\\ No newline at end of file") {
            changes += "\\nonnl\n";
            continue;
        }
        if (line.isEmpty() && i + 1 == lines.size() && !oldLeft && !newLeft) continue;
        const bool old = line.startsWith('-') || line.startsWith(' ');
        const bool next = line.startsWith('+') || line.startsWith(' ');
        if ((!old && !next) || (old && !oldLeft) || (next && !newLeft)) return {};
        const int before = old ? oldLine++ : 0, after = next ? newLine++ : 0;
        patch.rows.insert(i, {before, after});
        oldLeft -= old; newLeft -= next;
        // 归因要求整个文件的变更方向、行号及内容一致，上下文行不获得作者标识。
        if (old != next)
            changes += QByteArray::number(old ? before : after) + line.toUtf8() + '\n';
    }
    if (oldLeft || newLeft || changes.isEmpty()) return {};
    patch.signature = digest(changes);
    return patch;
}
}
