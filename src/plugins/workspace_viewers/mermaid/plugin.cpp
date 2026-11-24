#include "../viewer_registry.h"
namespace exagent {
namespace { class Plugin final : public WorkspaceViewerPlugin {
public: ViewerDescriptor descriptor() const override { return {"mermaid","Mermaid","WorkspaceWebPlugin.qml",{"mmd","mermaid"},ViewerDescriptor::Text,2*1024*1024}; }
}; }
std::unique_ptr<WorkspaceViewerPlugin> makeMermaidViewer() { return std::make_unique<Plugin>(); }
}
