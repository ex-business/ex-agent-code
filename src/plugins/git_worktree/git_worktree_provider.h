#pragma once
#include "application/project/task_workspace_provider.h"
namespace exagent {
class GitWorktreeProvider final : public TaskWorkspaceProvider {
public:
    using TaskWorkspaceProvider::TaskWorkspaceProvider;
    QString id() const override { return QStringLiteral("git.worktree"); }
    QString actionTitle() const override { return QStringLiteral("新建任务工作树"); }
    bool managesResources() const override { return true; }
    void resourceInfo(const QString &path, const QString &branch, bool remove, ResourceReady ready) override;
    void loadFields(const QString &source, FieldsReady ready) override;
    void create(const QString &source, const QString &name, const QVariantMap &options, Completion done) override;
};
}
