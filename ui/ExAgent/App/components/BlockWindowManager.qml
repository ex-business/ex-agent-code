pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQml.Models
import ExAgent.Chat
import ExAgent.App as Design

Instantiator {
    id: manager
    required property ChatController controller
    model: controller.blockViews.windowModel
    delegate: Window {
        id: blockWindow
        required property string blockId
        required property string viewId
        readonly property var snapshot: {
            const version = manager.controller.blockViews.revision
            return manager.controller.blockViews.snapshot(blockId)
        }
        visible: true
        width: 780; height: 720
        minimumWidth: 420; minimumHeight: 320
        title: snapshot.title || Design.BlockRendererRegistry.labelFor(Number(snapshot.rendererKind || 0))
        color: Design.Theme.background
        onClosing: { const id = blockId; Qt.callLater(() => manager.controller.blockViews.reattach(id)) }
        ScrollView {
            anchors.fill: parent
            anchors.margins: 16
            contentWidth: availableWidth
            Design.BlockRenderer {
                width: parent.width
                block: blockWindow.snapshot
                controller: manager.controller
                detached: true
                onLinkActivated: (link) => { if (link.startsWith("https://") || link.startsWith("http://")) Qt.openUrlExternally(link) }
            }
        }
    }
}
