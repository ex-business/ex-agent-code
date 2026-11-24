#pragma once
#include <QVariantMap>
#include <QMutex>
#include <memory>
#include <vector>
namespace exagent {
// 插件声明读取方式和界面入口，宿主不根据扩展名拼装专用查看器。
struct ViewerDescriptor {
    enum Storage { Text = 0, Image = 1, Managed = 2 };
    QString id, title, component;
    QStringList extensions;
    Storage storage = Text;
    qint64 maximumBytes = 2*1024*1024;
    QVariantMap map() const;
};
class WorkspaceViewerPlugin {
public:
    virtual ~WorkspaceViewerPlugin() = default;
    virtual ViewerDescriptor descriptor() const = 0;
    virtual QVariantMap load(const QByteArray &, const QString &) const { return {}; }
};
class WorkspaceViewerRegistry {
public:
    static WorkspaceViewerRegistry &instance();
    bool registerPlugin(std::unique_ptr<WorkspaceViewerPlugin> plugin, QString *error=nullptr);
    ViewerDescriptor resolve(const QString &path) const;
    QVariantList descriptors() const;
    QVariantMap load(const QString &path, const QString &root) const;
private:
    WorkspaceViewerRegistry();
    std::vector<std::unique_ptr<WorkspaceViewerPlugin>> m_plugins;
    mutable QMutex m_mutex;
    mutable bool m_sealed=false;
};
std::unique_ptr<WorkspaceViewerPlugin> makeHtmlViewer();
std::unique_ptr<WorkspaceViewerPlugin> makeMermaidViewer();
std::unique_ptr<WorkspaceViewerPlugin> makePdfViewer();
std::unique_ptr<WorkspaceViewerPlugin> makeTableViewer();
std::unique_ptr<WorkspaceViewerPlugin> makeNotebookViewer();
}
