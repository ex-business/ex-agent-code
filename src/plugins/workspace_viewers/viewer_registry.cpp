#include "viewer_registry.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMutexLocker>
#include <QRegularExpression>
namespace exagent {
QVariantMap ViewerDescriptor::map() const {
    return {{"id",id},{"title",title},{"component",component},{"storage",int(storage)},
            {"sourceToggle",storage==Text},{"maximumBytes",maximumBytes},{"extensions",extensions},{"apiVersion",1}};
}
namespace {
class Builtin final : public WorkspaceViewerPlugin {
public:
    explicit Builtin(ViewerDescriptor value): m_value(std::move(value)) {}
    ViewerDescriptor descriptor() const override { return m_value; }
private: ViewerDescriptor m_value;
};
}
WorkspaceViewerRegistry::WorkspaceViewerRegistry() {
    m_plugins.push_back(makeHtmlViewer()); m_plugins.push_back(makeMermaidViewer());
    m_plugins.push_back(makePdfViewer()); m_plugins.push_back(makeTableViewer()); m_plugins.push_back(makeNotebookViewer());
    m_plugins.push_back(std::make_unique<Builtin>(ViewerDescriptor{"image","图片","WorkspaceImagePlugin.qml",{"png","jpg","jpeg","webp","bmp","gif"},ViewerDescriptor::Image,32*1024*1024}));
    m_plugins.push_back(std::make_unique<Builtin>(ViewerDescriptor{"markdown","Markdown","WorkspaceMarkdownView.qml",{"md","markdown","mdown"}}));
}
WorkspaceViewerRegistry &WorkspaceViewerRegistry::instance() { static WorkspaceViewerRegistry registry; return registry; }
bool WorkspaceViewerRegistry::registerPlugin(std::unique_ptr<WorkspaceViewerPlugin> plugin,QString *error) {
    QMutexLocker lock(&m_mutex);
    auto reject=[error](const QString &reason){ if(error) *error=reason; return false; };
    // 注册完成后冻结，后台读取不会遇到插件被替换或卸载。
    if(m_sealed || !plugin) return reject(QStringLiteral("查看器注册已冻结或插件为空。"));
    const auto value=plugin->descriptor();
    const QRegularExpression identifier("^[a-z][a-z0-9-]{0,63}$"),component("^[A-Za-z][A-Za-z0-9]*\\.qml$");
    if(!identifier.match(value.id).hasMatch() || value.id=="source" || !component.match(value.component).hasMatch() || value.extensions.isEmpty() || value.maximumBytes<=0 || value.maximumBytes>64*1024*1024 || value.storage<ViewerDescriptor::Text || value.storage>ViewerDescriptor::Managed)
        return reject(QStringLiteral("查看器声明无效。"));
    for(const auto &extension:value.extensions) if(!identifier.match(extension).hasMatch()) return reject(QStringLiteral("查看器扩展名无效。"));
    for(const auto &registered:m_plugins) {
        const auto existing=registered->descriptor();
        if(existing.id==value.id) return reject(QStringLiteral("查看器标识重复。"));
        for(const auto &extension:value.extensions) if(existing.extensions.contains(extension)) return reject(QStringLiteral("查看器扩展名重复。"));
    }
    m_plugins.push_back(std::move(plugin)); return true;
}
ViewerDescriptor WorkspaceViewerRegistry::resolve(const QString &path) const {
    QMutexLocker lock(&m_mutex); m_sealed=true;
    const auto suffix=QFileInfo(path).suffix().toLower();
    for (const auto &plugin:m_plugins) { const auto value=plugin->descriptor(); if (value.extensions.contains(suffix)) return value; }
    return {"source","源代码","WorkspaceSourcePlugin.qml",{},ViewerDescriptor::Text,8*1024*1024};
}
QVariantMap WorkspaceViewerRegistry::load(const QString &path, const QString &root) const {
    const auto canonical=QFileInfo(path).canonicalFilePath(), base=QFileInfo(root).canonicalFilePath();
    if (base.isEmpty() || base!=root || canonical.isEmpty() || !canonical.startsWith(base.endsWith('/') ? base : base+'/')) return {{"error",QStringLiteral("文件不在当前工作区中。")}};
    const auto descriptor=resolve(canonical); QFile file(canonical);
    if (file.size()>descriptor.maximumBytes || !file.open(QIODevice::ReadOnly)) return {{"error",QStringLiteral("文件无法读取或超过查看器大小限制。")}};
    const auto bytes=file.read(descriptor.maximumBytes+1);
    if (file.error()!=QFile::NoError) return {{"error",QStringLiteral("文件读取失败，未发布不完整内容。")}};
    if (bytes.size()>descriptor.maximumBytes) return {{"error",QStringLiteral("文件读取期间超过大小限制。")}};
    for (const auto &plugin:m_plugins) if (plugin->descriptor().id==descriptor.id) return plugin->load(bytes,QFileInfo(canonical).suffix().toLower());
    return {{"error",QStringLiteral("查看器不支持此文件。")}};
}
QVariantList WorkspaceViewerRegistry::descriptors() const { QMutexLocker lock(&m_mutex); m_sealed=true; QVariantList list; for (const auto &plugin:m_plugins) list.append(plugin->descriptor().map()); return list; }
}
