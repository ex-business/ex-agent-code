#pragma once
#include "domain/project/project_catalog.h"
#include "domain/chat/chat_message.h"
#include <QObject>
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
    void registerTaskProvider(TaskWorkspaceProvider *provider);
    QVariantList taskProviders() const;
    Q_INVOKABLE void createTask(const QString &providerId, const QString &projectId, const QString &workspaceId, const QString &name, const QVariantMap &options = {});
    Q_INVOKABLE void loadTaskFields(const QString &providerId, const QString &projectId, const QString &workspaceId, int request);
    ProjectNavigator() = default;
    const QVariantList &rows() const { return m_rows; }
    bool writable() const { return m_writable; }
    void setWritable(bool value) { m_writable = value; }
    void refresh(const QList<ChatSession> &sessions);
    Q_INVOKABLE void createProject(const QString &name);
    Q_INVOKABLE void bindWorkspace(const QString &projectId, const QUrl &folder);
signals:
    void taskFieldsReady(int request, QVariantList fields);
    void rowsChanged();
    void openGroup(QString projectId, QString workspaceId);
    void failure(QString reason);
private:
    QList<TaskWorkspaceProvider *> m_providers;
    QVariantList m_rows;
    bool m_writable = true;
};
}
