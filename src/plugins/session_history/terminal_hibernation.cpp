#include "terminal_hibernation.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
namespace exagent::hibernation {
namespace {
    QString root()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/terminal-hibernation";
    }
    bool write(const QString& path, const QByteArray& data)
    {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return f.write(data) == data.size() && f.commit();
    }
}
QString directory(const QString& id)
{
    if (!id.startsWith("ex-") || QUuid(id.mid(3)).isNull())
        return {};
    return root() + "/" + id;
}
QString installExtension()
{
    QFile activity(":/terminal-hibernation/pi_activity.mjs");
    if (!QDir().mkpath(root()) || !activity.open(QIODevice::ReadOnly)
        || !write(root() + "/pi_activity.mjs", activity.readAll())) return {};
    QFile src(":/terminal-hibernation/pi_hibernation.mjs");
    // Pi 只接受文件路径；从程序资源原子发布与当前版本匹配的扩展。
    const auto path = root() + "/pi_hibernation.mjs";
    if (!QDir().mkpath(root()) || !src.open(QIODevice::ReadOnly) || !write(path, src.readAll()))
        return {};
    return path;
}
void permit(const QString& id, int minutes, qint64 since)
{
    const auto dir = directory(id);
    if (!dir.isEmpty() && QDir().mkpath(dir))
        write(dir + "/permit",
            QJsonDocument(QJsonObject { { "minutes", minutes }, { "since", since },
                              { "validUntil", QDateTime::currentMSecsSinceEpoch() + 15000 } })
                .toJson());
}
void revoke(const QString& id)
{
    const auto dir = directory(id);
    if (!dir.isEmpty())
        QFile::remove(dir + "/permit");
}
bool requested(const QVariantMap& tab)
{
    const auto dir = directory(tab.value("id").toString());
    QFile f(dir + "/sleep");
    if (dir.isEmpty() || !f.open(QIODevice::ReadOnly))
        return false;
    auto obj = QJsonDocument::fromJson(f.read(8192)).object();
    return !tab.value("origin").toString().isEmpty()
        && obj.value("origin").toString() == tab.value("origin").toString();
}
bool sameTranscript(const QVariantMap& tab)
{
    QFile marker(directory(tab.value("id").toString()) + "/sleep");
    QFile transcript(tab.value("origin").toString());
    if (!marker.open(QIODevice::ReadOnly) || !transcript.open(QIODevice::ReadOnly)) return false;
    const auto session = QJsonDocument::fromJson(marker.read(8192)).object().value("session").toString();
    const auto header = QJsonDocument::fromJson(transcript.readLine(65536)).object();
    return !QUuid(session).isNull() && header.value("type") == "session" && header.value("id") == session;
}
QString frame(const QString& id)
{
    QFile f(directory(id) + "/frame");
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(2 * 1024 * 1024)) : QString {};
}
}
