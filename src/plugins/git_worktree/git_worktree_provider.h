#pragma once
#include "application/project/task_workspace_provider.h"
namespace exagent {
class GitWorktreeProvider final : public TaskWorkspaceProvider {
public:
    using TaskWorkspaceProvider::TaskWorkspaceProvider;
    QString id() const override { return QStringLiteral("git.worktree"); }
    QString actionTitle() const override { return QStringLiteral("新建任务工作树"); }
    void create(const QString &source, const QString &name, Completion done) override;
};
}
