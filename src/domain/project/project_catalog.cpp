#include "project_catalog.h"

namespace exagent {
const Project *ProjectCatalog::find(const QString &id) const
{
    for (const auto &project : projects)
        if (project.id == id) return &project;
    return nullptr;
}

const WorkspaceBinding *ProjectCatalog::workspace(const QString &projectId, const QString &id) const
{
    const auto *project = find(projectId);
    if (project) for (const auto &workspace : project->workspaces)
        if (workspace.id == id) return &workspace;
    return nullptr;
}
}
