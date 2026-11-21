#include "conversation_resource_repository.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace exagent {
namespace {
QJsonDocument readDocument(const QString &path, const QString &root, QString *error)
{
    QFile file(path);
    // 只读取会话资源目录内的普通文件，避免清单或符号链接越过本地资源边界。
    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    const auto canonicalFile = QFileInfo(path).canonicalFilePath();
    if (canonicalRoot.isEmpty() || !canonicalFile.startsWith(canonicalRoot + '/')
        || file.size() > 8 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取会话资源：%1").arg(QFileInfo(path).fileName());
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) *error = QStringLiteral("会话资源 JSON 格式不正确：%1").arg(QFileInfo(path).fileName());
    return document;
}
}
QString ConversationResourceRepository::rootPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/conversations";
}
ConversationResource ConversationResourceRepository::manifest(const QString &id, QString *error) const
{
    static const QRegularExpression safeId("^[A-Za-z0-9_-]+$");
    if (!safeId.match(id).hasMatch()) { *error = QStringLiteral("会话资源标识不合法。"); return {}; }
    const auto document = readDocument(rootPath() + '/' + id + "/manifest.json", rootPath(), error);
    if (!error->isEmpty()) return {};
    const auto object = document.object();
    const int count = object.value("messageCount").toInt(-1);
    const int pages = object.value("pageCount").toInt(-1);
    if (!document.isObject() || object.value("schemaVersion").toInt() != 1 || object.value("id").toString() != id
        || object.value("title").toString().isEmpty() || count <= 0 || count > 1000000
        || object.value("pageSize").toInt() != 30 || pages != (count + 29) / 30) {
        *error = QStringLiteral("会话资源清单不完整或版本不支持。"); return {};
    }
    return {id, object.value("title").toString(), count, pages};
}
QVariantList ConversationResourceRepository::navigation() const
{
    QString error;
    const auto document = readDocument(rootPath() + "/navigation.json", rootPath(), &error);
    if (!error.isEmpty() || document.object().value("schemaVersion").toInt() != 1) return {};
    return document.object().value("entries").toArray().toVariantList();
}

QVariantList ConversationResourceRepository::list() const
{
    QVariantList result;
    for (const auto &directory : QDir(rootPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString error;
        const auto entry = manifest(directory, &error);
        if (error.isEmpty()) result.append(QVariantMap{{"id", entry.id}, {"title", entry.title}, {"messageCount", entry.messageCount}});
    }
    return result;
}
QList<ChatMessage> ConversationResourceRepository::page(const ConversationResource &resource, int number, QString *error) const
{
    if (number < 0 || number >= resource.pageCount) { *error = QStringLiteral("会话资源分页超出范围。"); return {}; }
    const auto path = rootPath() + '/' + resource.id + QString("/messages/%1.json").arg(number, 6, 10, QChar('0'));
    const auto document = readDocument(path, rootPath(), error);
    if (!error->isEmpty()) return {};
    const auto array = document.array();
    const int expected = qMin(30, resource.messageCount - number * 30);
    if (!document.isArray() || array.size() != expected) { *error = QStringLiteral("会话资源分页条数不正确。"); return {}; }
    QList<ChatMessage> result;
    for (const auto &value : array) {
        const auto object = value.toObject();
        const int role = object.value("role").toInt(-1);
        const int status = object.value("status").toInt(-1);
        if (role < 1 || role > 4 || status < 0 || status > 3 || object.value("id").toString().isEmpty()
            || !object.value("content").isString()) { *error = QStringLiteral("会话资源包含未知消息类型或状态。"); return {}; }
        ChatMessage message;
        message.id = object.value("id").toString();
        message.role = static_cast<MessageRole>(role);
        message.status = static_cast<MessageStatus>(status);
        message.content = object.value("content").toString();
        message.title = object.value("title").toString();
        message.createdAt = QDateTime::fromString(object.value("createdAt").toString(), Qt::ISODateWithMs);
        result.append(message);
    }
    return result;
}
}
