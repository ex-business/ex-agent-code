#include "history_migration.h"
#include "conversation_image.h"
#include "conversation_resource_repository.h"
#include "project_repository.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace exagent {
namespace {
struct HistoricalGroup {
    QString id;
    QString title;
};

QHash<QString, HistoricalGroup> historicalGroups(const QVariantList &navigation)
{
    QHash<QString, HistoricalGroup> groups;
    HistoricalGroup group;
    for (const auto &value : navigation) {
        const auto row = value.toMap();
        const auto title = row.value("title").toString();
        const auto kind = row.value("kind").toInt();
        if (kind == 0) {
            group = {};
            if (title == QStringLiteral("展示资源")) group = {QStringLiteral("history.section.examples"), title};
        } else if (kind == 2) {
            group = {QStringLiteral("history.project.") + title, title};
        } else if (kind == 3 && !group.title.isEmpty()) {
            // 置顶和仅有标题的快捷方式不提供项目归属；以项目下的实际资源条目为准。
            const auto resource = row.value("resourceId").toString();
            if (!resource.isEmpty()) groups.insert(resource, group);
        }
    }
    return groups;
}

bool importMessages(const ConversationResourceRepository &resources, const ConversationResource &resource,
                    const QString &directory, ChatSession &session, QString *error)
{
    QSet<QString> ids;
    for (int page = 0; page < resource.pageCount; ++page) {
        auto messages = resources.page(resource, page, error, true);
        if (!error->isEmpty()) return false;
        for (auto &message : messages) {
            if (ids.contains(message.id)) {
                *error = QStringLiteral("会话中含有重复的消息标识。");
                return false;
            }
            ids.insert(message.id);
            for (auto &value : message.attachments) {
                auto attachment = value.toMap();
                const auto source = QUrl(attachment.value("url").toString()).toLocalFile();
                const auto copy = importConversationImage(source, directory, attachment.value("name").toString());
                if (!copy.error.isEmpty()) { *error = copy.error; return false; }
                attachment.remove("path");
                attachment.insert(copy.attachment);
                value = attachment;
            }
        }
        session.messages.append(std::move(messages));
    }
    if (session.messages.last().createdAt.isValid()) session.updatedAt = session.messages.last().createdAt;
    return true;
}
}

bool migrateHistory(const QString &directory, QString *error)
{
    ProjectRepository repository(directory);
    ProjectCatalog catalog;
    auto sessions = repository.load(catalog, error);
    if (!error->isEmpty()) return false;
    ConversationResourceRepository resources;
    const auto navigation = resources.navigation(error);
    if (!error->isEmpty()) return false;
    const auto groups = historicalGroups(navigation);
    const bool firstOpen = sessions.isEmpty();
    QSet<QString> existingIds;
    QHash<QString, QString> imported;
    for (const auto &session : std::as_const(sessions)) {
        existingIds.insert(session.id);
        const auto source = session.history.value("resourceId").toString();
        if (!source.isEmpty()) imported.insert(source, session.id);
    }
    // 清单目录与导航分别枚举：无项目的有效历史归入未归档，无正文的导航标题不创建会话。
    const auto dirs = QDir(resources.rootPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &id : dirs) {
        if (imported.contains(id) || !QFileInfo::exists(resources.rootPath() + '/' + id + "/manifest.json")) continue;
        const auto resource = resources.manifest(id, error);
        if (!error->isEmpty()) return false;
        const auto group = groups.value(id);
        ChatSession session;
        // 稳定目录使失败后重试复用已写附件，资源名与原有本地会话仍使用独立身份。
        static const QUuid namespaceId(QStringLiteral("a15d899c-1496-4d91-a456-fdadcf21668b"));
        session.id = QUuid::createUuidV5(namespaceId, id.toUtf8()).toString(QUuid::WithoutBraces);
        if (existingIds.contains(session.id)) {
            *error = QStringLiteral("历史会话标识与已有会话冲突。");
            return false;
        }
        existingIds.insert(session.id);
        session.title = resource.title;
        session.projectId = group.id.isEmpty() ? ProjectCatalog::inboxId() : group.id;
        if (!catalog.find(session.projectId)) catalog.projects.append({group.id, group.title, {}});
        session.history = {{"resourceId", id}, {"source", resource.source}, {"changeSummary", resource.changeSummary},
                           {"activityStatus", resource.activityStatus}};
        if (!importMessages(resources, resource, repository.sessionDirectory(session.projectId, session.id), session, error)) {
            *error = QStringLiteral("转换「%1」失败：%2").arg(resource.title, *error);
            return false;
        }
        imported.insert(id, session.id);
        sessions.append(std::move(session));
    }
    if (imported.contains(catalog.selectedResource)) {
        catalog.selectedSession = imported.value(catalog.selectedResource);
        catalog.selectedResource.clear();
    } else if (firstOpen && catalog.selectedResource.isEmpty()) {
        catalog.selectedSession = imported.value(QStringLiteral("webchat-go"));
    }
    // 只在全部分页和附件成功后发布；重试以资源标识去重，保留用户后续编辑。
    return repository.save(catalog, sessions, error);
}
}
