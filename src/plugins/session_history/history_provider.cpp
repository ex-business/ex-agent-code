#include "history_provider.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace exagent {
QString cliPath(const QString& name)
{
    const auto found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable(
        name, { "/opt/homebrew/bin", "/usr/local/bin", QDir::homePath() + "/.local/bin" });
}

QList<HistoryProvider> installedHistoryFormats()
{
    const auto home = QDir::homePath();
    return { { "Pi", "pi",
                 qEnvironmentVariable("PI_CODING_AGENT_SESSION_DIR",
                     qEnvironmentVariable("PI_CODING_AGENT_DIR", home + "/.pi/agent") + "/sessions"),
                 "--session", true },
        { "Claude Code", "claude", qEnvironmentVariable("CLAUDE_CONFIG_DIR", home + "/.claude") + "/projects",
            "--resume", false } };
}

QVariantList discoverSessions(const QString& workspace)
{
    QVariantList rows;
    for (const auto& p : installedHistoryFormats()) {
        const auto executable = cliPath(p.executable);
        // Pi 也允许直接指定会话目录；Claude 的子 Agent 日志不作为独立主会话。
        auto directories = QDir(p.directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        if (p.resumeByFile) directories.prepend(QFileInfo(p.directory));
        for (const auto& dir : directories) {
            for (const auto& info :
                QDir(dir.absoluteFilePath()).entryInfoList({ "*.jsonl" }, QDir::Files | QDir::NoSymLinks)) {
                QFile file(info.absoluteFilePath());
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                QString cwd, id;
                for (int i = 0; i < 256 && file.pos() < 1024 * 1024 && !file.atEnd(); ++i) {
                    const auto line = file.readLine(256 * 1024);
                    const auto obj = QJsonDocument::fromJson(line).object();
                    if (p.resumeByFile && obj["type"] != "session")
                        break;
                    id = obj[p.resumeByFile ? "id" : "sessionId"].toString();
                    cwd = obj["cwd"].toString();
                    if (!cwd.isEmpty() && !QUuid(id).isNull())
                        break;
                }
                if (!QDir::isAbsolutePath(cwd) || QUuid(id).isNull())
                    continue;
                if (!p.resumeByFile && info.completeBaseName() != id)
                    continue;
                if (!workspace.isEmpty() && QDir::cleanPath(cwd) != QDir::cleanPath(workspace))
                    continue;
                rows.append(QVariantMap { { "agent", p.name }, { "program", executable }, { "flag", p.resumeFlag },
                    { "argument", p.resumeByFile ? info.absoluteFilePath() : id }, { "id", id }, { "cwd", cwd },
                    { "file", info.absoluteFilePath() }, { "updated", info.lastModified().toMSecsSinceEpoch() } });
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["updated"].toLongLong() > b.toMap()["updated"].toLongLong();
    });
    return rows;
}
}
