#include "../viewer_registry.h"
namespace exagent {
namespace { class Plugin final : public WorkspaceViewerPlugin {
public: ViewerDescriptor descriptor() const override { return {"html","HTML","WorkspaceWebPlugin.qml",{"html","htm"},ViewerDescriptor::Text,2*1024*1024}; }
}; }
std::unique_ptr<WorkspaceViewerPlugin> makeHtmlViewer() { return std::make_unique<Plugin>(); }
}
