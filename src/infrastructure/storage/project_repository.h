#pragma once

#include "domain/project/project_catalog.h"
#include "session_repository.h"
#include <QHash>
#include <QLockFile>

namespace exagent {

// 索引只登记归属和选择状态；复用旧版消息编解码，每个可写会话独立保存。
class ProjectRepository {
public:
    explicit ProjectRepository(QString directory);
    QList<ChatSession> load(ProjectCatalog &catalog, QString *error);
    bool save(const ProjectCatalog &catalog, const QList<ChatSession> &sessions, QString *error);
    QString filePath() const;
    QString sessionDirectory(const QString &project, const QString &session) const;
private:
    QString m_directory;
    QLockFile m_lock;
    QHash<QString, ChatSession> m_saved;
    QHash<QString, int> m_generations;
    QByteArray m_savedIndex;
};
}
