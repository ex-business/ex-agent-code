#pragma once
#include "domain/project/project_catalog.h"
#include "domain/chat/chat_message.h"
#include <QObject>
#include <QSet>
#include "task_workspace_provider.h"
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class ProjectNavigator final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectNavigator)
    QML_UNCREATABLE("由宿主注入项目导航")
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList taskProviders READ taskProviders CONSTANT)
    Q_PROPERTY(bool writable READ writable CONSTANT)
public:
    enum RowKind { ProjectRow = 1, WorkspaceRow, SessionRow };
    Q_ENUM(RowKind)
    ProjectCatalog catalog;
    std::function<QStringList(const QString &)> runningTasks;
    std::function<bool()> saveCatalog;
    Q_INVOKABLE void manageResource(const QString &provider, const QString &project, const QString &workspace,
                                    int action, const QString &branch);
    enum ResourceAction { Inspect, Archive, Restore, Clean };
    Q_ENUM(ResourceAction)
    bool available(const QString &project, const QString &workspace) const;
    bool setCheckpoint(const QString &project, const QString &workspace, const QString &comment, int state, int revision);
    void registerTaskProvider(TaskWorkspaceProvider *provider);
    QVariantList taskProviders() const;
    Q_INVOKABLE void createTask(const QString &providerId, const QString &projectId, const QString &workspaceId, const QString &name, const QVariantMap &options = {});
    Q_INVOKABLE void loadTaskFields(const QString &providerId, const QString &projectId, const QString &workspaceId, int request);
    Q_INVOKABLE QVariantMap launchSettings(const QString &provider, const QString &project, const QString &workspace) const;
    Q_INVOKABLE QString createLaunchTemplate(const QString &project, const QString &workspace);
    ProjectNavigator() = default;
    const QVariantList &rows() const { return m_rows; }
    bool writable() const { return m_writable; }
    void setWritable(bool value) { m_writable = value; }
    void refresh(const QList<ChatSession> &sessions);
    Q_INVOKABLE void createProject(const QString &name);
    Q_INVOKABLE void bindWorkspace(const QString &projectId, const QUrl &folder);
signals:
    void resourceReport(QString project, QString workspace, QVariantMap report);
    void catalogChanged();
    void taskFieldsReady(int request, QVariantList fields);
    void rowsChanged();
    void openGroup(QString projectId, QString workspaceId);
    void failure(QString reason);
private:
    void bindCreatedWorkspace(const QString &projectId, const QUrl &folder, const QVariantMap &launch);
    QHash<QString, QStringList> m_preparing;
    QSet<QString> m_changing;
    QList<TaskWorkspaceProvider *> m_providers;
    QVariantList m_rows;
    bool m_writable = true;
};
}
