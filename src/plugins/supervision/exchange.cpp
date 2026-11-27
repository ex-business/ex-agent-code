#include "exchange.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>

namespace exagent::supervision {
QJsonObject readEnvelope(const QString &path, QString *error)
{
    const QFileInfo info(path);
    QFile file(path);
    if (!info.isAbsolute() || info.isSymLink() || !info.isFile() || info.size() > 65536 || !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("协作文件不可读取或超过 64 KiB。");
        return {};
    }
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError) {
        *error = QStringLiteral("协作文件读取失败。");
        return {};
    }
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.object()["version"].toDouble(-1) != 1.0) {
        *error = QStringLiteral("协作文件版本或格式无效。");
        return {};
    }
    return doc.object();
}

bool writeEnvelope(const QString &root, const QString &name, const QJsonObject &data, QString *error)
{
    const QFileInfo folder(root);
    if (!folder.isAbsolute() || !folder.isDir() || folder.canonicalFilePath() != root || name.contains('/') || name.contains('\\')) {
        *error = QStringLiteral("请选择已存在的协作交换目录。");
        return false;
    }
    const auto path = QDir(root).filePath(name);
    QLockFile lock(path + ".lock");
    if (!lock.tryLock()) { *error = QStringLiteral("协作文件正在写入，请稍后重试。"); return false; }
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (bytes.size() > 65536 || QFileInfo(path).isSymLink()) return false;
    if (QFileInfo::exists(path)) {
        QString readError;
        if (readEnvelope(path, &readError) == data) return true;
        *error = QStringLiteral("同一派发已有不同的协作记录，已保留原文件。");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { *error = file.errorString(); return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    return true;
}
}
