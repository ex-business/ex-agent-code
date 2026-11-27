#pragma once

#include <QList>
#include <QStringList>
#include <QVariantMap>

namespace exagent {

enum class CheckpointState {
  Todo = 0,
  InProgress = 1,
  InReview = 2,
  Completed = 3
};

enum class WorkspaceState { Active = 0, Archived = 1, Cleaned = 2 };

struct WorkspaceBinding {
  QString id;
  QString path;
  QStringList taskLinks{};
  QVariantMap launch = {};
  WorkspaceState state = WorkspaceState::Active;
  QString branch = {};
  QVariantMap checkpoint = {};
};

enum class ProjectKind { Normal = 1, Group = 2 };

struct Project {
  QString id;
  QString name;
  QList<WorkspaceBinding> workspaces;
  QVariantMap organization = {};
  ProjectKind kind() const {
    return ProjectKind(organization.value("kind", 1).toInt());
  }
  QString groupId() const { return organization.value("group").toString(); }
  QString rootPath() const { return organization.value("root").toString(); }
};

// 项目身份与本机路径独立。旧聊天和没有目录的会话始终有可用归属。
struct ProjectCatalog {
  static QString inboxId() { return QStringLiteral("inbox"); }
  QList<Project> projects{{inboxId(), QStringLiteral("未归档"), {}}};
  QString selectedSession;
  QString selectedResource;
  const Project *find(const QString &id) const;
  const WorkspaceBinding *workspace(const QString &projectId,
                                    const QString &id) const;
};

bool validOrganization(const ProjectCatalog &catalog);
} // namespace exagent
