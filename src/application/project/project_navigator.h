#pragma once
#include "domain/project/project_catalog.h"
#include "domain/chat/chat_message.h"
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class ProjectNavigator final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectNavigator)
    QML_UNCREATABLE("由宿主注入项目导航")
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(bool writable READ writable CONSTANT)
public:
    enum RowKind { ProjectRow = 1, WorkspaceRow, SessionRow };
    Q_ENUM(RowKind)
    ProjectCatalog catalog;
    ProjectNavigator() = default;
    const QVariantList &rows() const { return m_rows; }
    bool writable() const { return m_writable; }
    void setWritable(bool value) { m_writable = value; }
    void refresh(const QList<ChatSession> &sessions);
    Q_INVOKABLE void createProject(const QString &name);
    Q_INVOKABLE void bindWorkspace(const QString &projectId, const QUrl &folder);
signals:
    void rowsChanged();
    void openGroup(QString projectId, QString workspaceId);
    void failure(QString reason);
private:
    QVariantList m_rows;
    bool m_writable = true;
};
}
