#include "project_repository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace exagent {
namespace {
QString snapshotName(int slot)
{
    return slot < 0 ? QStringLiteral("sessions.json") : QStringLiteral("history-%1.json").arg(slot);
}
}

ProjectRepository::ProjectRepository(QString directory)
    : m_directory(std::move(directory)), m_lock(m_directory + "/project-index.lock") {}

QString ProjectRepository::filePath() const { return m_directory + "/project-index.json"; }

QString ProjectRepository::sessionDirectory(const QString &project, const QString &session) const
{
    // 历史 ID 可能包含路径字符；存储键只由摘要组成，业务 ID 原样保存在记录中。
    const auto key = [](const QString &id) {
        return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
    };
    return m_directory + "/projects/" + key(project) + "/conversations/" + key(session);
}

QList<ChatSession> ProjectRepository::load(ProjectCatalog &catalog, QString *error)
{
    if (!QDir().mkpath(m_directory)) {
        *error = QStringLiteral("无法打开本地项目目录。");
        return {};
    }
    // 同一份索引只允许一个宿主写入，其他实例可以阅读上一次成功保存的数据。
    m_lock.setStaleLockTime(std::chrono::milliseconds::zero());
    const bool writable = m_lock.tryLock();
    QFile file(filePath());
    QList<ChatSession> sessions;
    if (!file.exists()) {
        sessions = SessionRepository(m_directory).load(error);
        for (auto &session : sessions) session.projectId = ProjectCatalog::inboxId();
    } else {
        const auto invalid = [&]() -> QList<ChatSession> {
            *error = QStringLiteral("项目索引或会话归属不完整，已暂停写回。");
            return {};
        };
        if (!file.open(QIODevice::ReadOnly)) return invalid();
        m_savedIndex = file.readAll();
        const auto root = QJsonDocument::fromJson(m_savedIndex).object();
        if (root["version"].toInt() != 1 || !root["projects"].isArray() || !root["sessions"].isArray()) return invalid();
        ProjectCatalog restored;
        restored.projects.clear();
        QSet<QString> workspaceIds;
        for (const auto &entry : root["projects"].toArray()) {
            const auto row = entry.toObject();
            Project project{row["id"].toString(), row["name"].toString(), {}};
            if (project.id.isEmpty() || project.name.isEmpty() || restored.find(project.id) || !row["workspaces"].isArray()) return invalid();
            for (const auto &item : row["workspaces"].toArray()) {
                const auto data = item.toObject();
                WorkspaceBinding workspace{data["id"].toString(), data["path"].toString()};
                if (workspace.id.isEmpty() || !QDir::isAbsolutePath(workspace.path) || workspaceIds.contains(workspace.id)) return invalid();
                for (const auto &link : data["tasks"].toArray())
                    if (link.isString()) workspace.taskLinks.append(link.toString());
                const int state = data.contains("state") ? data.value("state").toInt(-1) : 0;
                if (state < 0 || state > int(WorkspaceState::Cleaned)) return invalid();
                workspace.launch = data["launch"].toObject().toVariantMap();
                workspace.state = WorkspaceState(state);
                workspace.branch = data.value("branch").toString();
                workspaceIds.insert(workspace.id);
                project.workspaces.append(workspace);
            }
            restored.projects.append(project);
        }
        if (!restored.find(ProjectCatalog::inboxId())) return invalid();
        for (const auto &entry : root["sessions"].toArray()) {
            const auto row = entry.toObject();
            const auto id = row["id"].toString();
            const auto project = row["project"].toString();
            const auto workspace = row["workspace"].toString();
            if (id.isEmpty() || m_saved.contains(id) || !restored.find(project)
                || (!workspace.isEmpty() && !restored.workspace(project, workspace))) return invalid();
            const int slot = row.value("slot").toInt(-1);
            if (row.contains("slot") && slot != 0 && slot != 1) return invalid();
            m_generations.insert(id, slot);
            auto loaded = SessionRepository(sessionDirectory(project, id), snapshotName(slot)).load(error);
            if (!error->isEmpty()) return {};
            if (loaded.size() != 1 || loaded.first().id != id) return invalid();
            auto session = loaded.takeFirst();
            session.projectId = project;
            session.workspaceId = workspace;
            sessions.append(session);
            m_saved.insert(id, session);
        }
        restored.selectedSession = root["selectedSession"].toString();
        restored.selectedResource = root["selectedResource"].toString();
        catalog = std::move(restored);
    }
    if (!writable && error->isEmpty()) *error = QStringLiteral("另一窗口正在使用项目目录，当前以只读方式打开。");
    return sessions;
}

bool ProjectRepository::save(const ProjectCatalog &catalog, const QList<ChatSession> &sessions, QString *error)
{
    if (!m_lock.isLocked()) { *error = QStringLiteral("项目目录未获得写入权。"); return false; }
    QJsonArray projects, entries;
    for (const auto &project : catalog.projects) {
        QJsonArray workspaces;
        for (const auto &workspace : project.workspaces) {
            QJsonObject item{{"id", workspace.id}, {"path", workspace.path}};
            if (!workspace.taskLinks.isEmpty()) item.insert("tasks", QJsonArray::fromStringList(workspace.taskLinks));
            item.insert("launch", QJsonObject::fromVariantMap(workspace.launch));
            item.insert("state", int(workspace.state));
            item.insert("branch", workspace.branch);
            workspaces.append(item);
        }
        projects.append(QJsonObject{{"id", project.id}, {"name", project.name}, {"workspaces", workspaces}});
    }
    auto generations = m_generations;
    auto saved = m_saved;
    QSet<QString> ids;
    for (const auto &session : sessions) {
        if (ids.contains(session.id)) { *error = QStringLiteral("会话标识重复，已保留原索引。"); return false; }
        ids.insert(session.id);
        const auto cached = m_saved.constFind(session.id);
        if (cached == m_saved.cend() || cached.value() != session) {
            // 总是写入非活动槽位，索引发布前崩溃仍可恢复完整的上一批。
            const int slot = m_generations.value(session.id, -1) == 0 ? 1 : 0;
            if (!SessionRepository(sessionDirectory(session.projectId, session.id), snapshotName(slot)).save({session}, error)) return false;
            generations.insert(session.id, slot);
            saved.insert(session.id, session);
        }
        QJsonObject entry{{"id", session.id}, {"project", session.projectId}, {"workspace", session.workspaceId}};
        const int slot = generations.value(session.id, -1);
        if (slot >= 0) entry.insert("slot", slot);
        entries.append(entry);
    }
    // 先完整写入会话，再发布索引。迁移中断后仍可从旧 sessions.json 重试。
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"projects", projects}, {"sessions", entries},
        {"selectedSession", catalog.selectedSession}, {"selectedResource", catalog.selectedResource}}).toJson(QJsonDocument::Compact);
    if (m_savedIndex == bytes) return true;
    QSaveFile index(filePath());
    if (!index.open(QIODevice::WriteOnly)) { *error = QStringLiteral("无法写入项目索引。"); return false; }
    index.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (index.write(bytes) != bytes.size() || !index.commit()) { *error = QStringLiteral("项目索引保存失败。"); return false; }
    m_savedIndex = bytes;
    m_generations = std::move(generations);
    m_saved = std::move(saved);
    return true;
}
}
