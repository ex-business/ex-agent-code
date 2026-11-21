#include "session_repository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <utility>

namespace exagent {

SessionRepository::SessionRepository(QString directory) : m_directory(std::move(directory)) {}

QString SessionRepository::filePath() const { return QDir(m_directory).filePath("sessions.json"); }

QList<ChatSession> SessionRepository::load(QString *error) const
{
    QFile file(filePath());
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取本地会话：%1").arg(file.errorString());
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("schemaVersion").toInt(-1) != 1
        || !document.object().value("sessions").isArray()) {
        // 读取失败时由控制器禁止写回，保留原文件供恢复，不覆盖已有数据。
        *error = QStringLiteral("本地会话格式无法识别，已保留原文件并暂停保存。");
        return {};
    }
    QList<ChatSession> sessions;
    for (const auto &value : document.object().value("sessions").toArray()) {
        const auto object = value.toObject();
        ChatSession session;
        session.id = object.value("id").toString();
        session.title = object.value("title").toString();
        session.draft = object.value("draft").toString();
        session.updatedAt = QDateTime::fromString(object.value("updatedAt").toString(), Qt::ISODateWithMs);
        if (session.id.isEmpty() || !object.value("messages").isArray()) {
            *error = QStringLiteral("会话记录不完整，已暂停写回以保护已有内容。");
            return {};
        }
        for (const auto &entry : object.value("messages").toArray()) {
            const auto messageObject = entry.toObject();
            const int role = messageObject.value("role").toInt(-1);
            const int status = messageObject.value("status").toInt(-1);
            if (role < 1 || role > 4 || status < 0 || status > 3
                || messageObject.value("id").toString().isEmpty()) {
                *error = QStringLiteral("消息包含未知状态，已暂停写回并保留原文件。");
                return {};
            }
            ChatMessage message;
            message.id = messageObject.value("id").toString();
            message.role = static_cast<MessageRole>(role);
            message.status = static_cast<MessageStatus>(status);
            message.content = messageObject.value("content").toString();
            message.title = messageObject.value("title").toString();
            message.expanded = messageObject.value("expanded").toBool();
            message.createdAt = QDateTime::fromString(messageObject.value("createdAt").toString(), Qt::ISODateWithMs);
            session.messages.append(message);
        }
        sessions.append(session);
    }
    return sessions;
}

bool SessionRepository::save(const QList<ChatSession> &sessions, QString *error) const
{
    if (!QDir().mkpath(m_directory)) {
        *error = QStringLiteral("无法创建本地会话目录。");
        return false;
    }
    QJsonArray sessionArray;
    for (const auto &session : sessions) {
        QJsonArray messages;
        for (const auto &message : session.messages) {
            messages.append(QJsonObject{
                {"id", message.id}, {"role", static_cast<int>(message.role)},
                {"status", static_cast<int>(message.status)}, {"content", message.content},
                {"title", message.title}, {"expanded", message.expanded},
                {"createdAt", message.createdAt.toString(Qt::ISODateWithMs)}});
        }
        sessionArray.append(QJsonObject{
            {"id", session.id}, {"title", session.title}, {"draft", session.draft},
            {"updatedAt", session.updatedAt.toString(Qt::ISODateWithMs)}, {"messages", messages}});
    }
    QSaveFile file(filePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法保存本地会话：%1").arg(file.errorString());
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"sessions", sessionArray}}).toJson();
    // 原子替换避免退出或写入失败留下半个 JSON；失败不删除旧数据。
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("本地会话保存失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace exagent
