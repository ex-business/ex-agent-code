#pragma once

#include <QList>
#include <QStringList>

namespace exagent {

struct WorkspaceBinding {
    QString id;
    QString path;
    QStringList taskLinks{};
};

struct Project {
    QString id;
    QString name;
    QList<WorkspaceBinding> workspaces;
};

// 项目身份与本机路径独立。旧聊天和没有目录的会话始终有可用归属。
struct ProjectCatalog {
    static QString inboxId() { return QStringLiteral("inbox"); }
    QList<Project> projects{{inboxId(), QStringLiteral("未归档"), {}}};
    QString selectedSession;
    QString selectedResource;
    const Project *find(const QString &id) const;
    const WorkspaceBinding *workspace(const QString &projectId, const QString &id) const;
};

}
