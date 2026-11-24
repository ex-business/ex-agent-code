#include "activity_store.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
namespace exagent::activity {
static QString digest(const QString& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString directory(const QString& cwd) {
    if (cwd.isEmpty() || !QFileInfo(cwd).isDir()) return {};
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/agent-activity/" + digest(QFileInfo(cwd).canonicalFilePath());
}
void publish(const QString& cwd, const QString& run, const QVariantMap& snapshot) {
    const auto dir = directory(cwd);
    if (dir.isEmpty() || run.isEmpty() || !QDir().mkpath(dir)) return;
    QSaveFile file(dir + "/codex-" + digest(run) + ".json");
    if (!file.open(QIODevice::WriteOnly)) return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto row = snapshot;
    row.insert("agent","Codex");
    row.insert("updated",QDateTime::currentMSecsSinceEpoch());
    const auto bytes = QJsonDocument::fromVariant(row).toJson(QJsonDocument::Compact);
    if (file.write(bytes) == bytes.size()) file.commit();
}
QVariantList recent(const QString& cwd) {
    QVariantList rows;
    const auto dir = directory(cwd);
    if (dir.isEmpty()) return rows;
    // 按最近上报时间展示，原子替换快照，重复通知不会累计 token。
    const auto files = QDir(dir).entryInfoList({"*.json"},QDir::Files | QDir::NoSymLinks,QDir::Time);
    for (const auto& info : files) {
        if (rows.size() >= 50) break;
        QFile file(info.absoluteFilePath());
        if (info.size()>8192 || !file.open(QIODevice::ReadOnly)) continue;
        const auto row = QJsonDocument::fromJson(file.read(8193)).toVariant().toMap();
        if (row.contains("agent") && row.contains("status")) rows.append(row);
    }
    return rows;
}
QVariantMap memory(const QString& cwd,const QString& relative) {
    const auto root = QFileInfo(cwd).canonicalFilePath();
    const QFileInfo file(QDir(root).filePath(relative));
    const auto path = file.canonicalFilePath();
    // 只读取工作区内明确选中的记忆文件，符号链接也必须落在同一工作区。
    if (cwd.isEmpty() || root.isEmpty() || QDir::isAbsolutePath(relative)
        || !QStringList{"AGENTS.md","CLAUDE.md","AGENTS.override.md"}.contains(file.fileName())
        || path.isEmpty()
        || !path.startsWith(root.endsWith('/') ? root : root + '/') || !file.isFile())
        return {{"error",QStringLiteral("未找到工作区内的记忆文件。")}};
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return {{"error",QStringLiteral("无法读取文件。")}};
    const auto data = input.read(65537);
    return {{"text",QString::fromUtf8(data.left(65536))},{"truncated",data.size()>65536}};
}
}
