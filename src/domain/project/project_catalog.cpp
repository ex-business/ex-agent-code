#include "project_catalog.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>

namespace exagent {
const Project *ProjectCatalog::find(const QString &id) const {
  for (const auto &project : projects)
    if (project.id == id)
      return &project;
  return nullptr;
}

const WorkspaceBinding *ProjectCatalog::workspace(const QString &projectId,
                                                  const QString &id) const {
  const auto *project = find(projectId);
  if (project)
    for (const auto &workspace : project->workspaces)
      if (workspace.id == id)
        return &workspace;
  return nullptr;
}
bool validOrganization(const ProjectCatalog &catalog) {
  QSet<QString> groups, members;
  for (const auto &p : catalog.projects) {
    if (p.organization.isEmpty())
      continue;
    const auto data = QJsonObject::fromVariantMap(p.organization);
    const int kind = data["kind"].toInt(-1);
    if (!data["kind"].isDouble() || data["kind"].toDouble() != kind ||
        (kind != 1 && kind != 2) || p.id == ProjectCatalog::inboxId() ||
        !data["root"].isString() || !QDir::isAbsolutePath(p.rootPath()) ||
        QDir::cleanPath(p.rootPath()) != p.rootPath() ||
        data.size() != (kind == 2 ? 2 : 3))
      return false;
    if (kind == 2) {
      if (data.contains("group") || groups.contains(p.rootPath()))
        return false;
      groups.insert(p.rootPath());
    } else {
      const auto *parent = catalog.find(p.groupId());
      if (!data["group"].isString() || !parent ||
          parent->kind() != ProjectKind::Group ||
          QFileInfo(p.rootPath()).absolutePath() != parent->rootPath() ||
          members.contains(p.groupId() + "/" + p.rootPath()))
        return false;
      members.insert(p.groupId() + "/" + p.rootPath());
    }
  }
  for (const auto &p : catalog.projects)
    for (const auto &w : p.workspaces) {
      if (w.launch.value("provider") == "folder.detached") {
        const auto repository = w.launch.value("repositoryRoot").toString();
        if (p.kind() != ProjectKind::Normal || !p.organization.isEmpty() ||
            !QDir::isAbsolutePath(repository) ||
            QDir::cleanPath(repository) != repository ||
            QFileInfo(repository).absolutePath() != w.path ||
            w.launch.value("host") != "local" ||
            w.launch.value("name").toString().isEmpty())
          return false;
        continue;
      }
      if (w.launch.value("provider").toString() != "folder.workspace")
        continue;
      const auto *source =
          catalog.find(w.launch.value("taskSource").toString());
      if (p.kind() != ProjectKind::Group || !source ||
          source->groupId() != p.id || w.path != p.rootPath() ||
          w.launch.value("host").toString() != "local" ||
          w.launch.value("name").toString().isEmpty())
        return false;
    }
  return true;
}

} // namespace exagent
