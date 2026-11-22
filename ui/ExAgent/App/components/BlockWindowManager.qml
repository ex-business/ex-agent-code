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
    required property Window hostWindow
    model: controller.blockViews.windowModel
    delegate: Window {
        id: blockWindow
        required property string blockId
        required property string viewId
        readonly property var snapshot: {
            const version = manager.controller.blockViews.revision
            return manager.controller.blockViews.snapshot(blockId)
        }
        readonly property bool mediaWindow: Number(snapshot.rendererKind) === 5 || Number(snapshot.rendererKind) === 6
        transientParent: manager.hostWindow
        visible: true
        width: mediaWindow ? 1000 : 780; height: mediaWindow ? 760 : 720
        minimumWidth: mediaWindow ? 680 : 420; minimumHeight: mediaWindow ? 520 : 320
        x: manager.hostWindow.x + Math.max(0, (manager.hostWindow.width - width) / 2)
        y: manager.hostWindow.y + Math.max(32, (manager.hostWindow.height - height) / 2)
        function activateContent() {
            if (!visible) return
            blockWindow.raise(); blockWindow.requestActivate()
        }
        Connections {
            target: manager.controller.blockViews
            function onFocusRequested(id: string) {
                if (id !== blockWindow.blockId) return
                blockWindow.show(); blockWindow.raise(); blockWindow.requestActivate()
            }
        }
        title: snapshot.title || Design.BlockRendererRegistry.labelFor(Number(snapshot.rendererKind || 0))
        color: Design.Theme.background
        onClosing: { const id = blockId; Qt.callLater(() => manager.controller.blockViews.reattach(id)) }
        Design.WindowEntrance {
            anchors.fill: parent
            ready: mediaLoader.status === Loader.Ready || contentLoader.status === Loader.Ready
            onReadyChanged: if (ready) Qt.callLater(blockWindow.activateContent)
        Loader {
            id: mediaLoader
            focus: true
            anchors.fill: parent
            active: Number(blockWindow.snapshot.rendererKind) === 5 || Number(blockWindow.snapshot.rendererKind) === 6
            sourceComponent: Design.MediaViewer { block: blockWindow.snapshot; controller: manager.controller }
        }
        Loader {
            id: contentLoader
            anchors.fill: parent
            active: Number(blockWindow.snapshot.rendererKind) !== 5 && Number(blockWindow.snapshot.rendererKind) !== 6
            sourceComponent: ScrollView {
                id: contentScroll
                anchors.fill: parent
                anchors.margins: 16
                contentWidth: availableWidth
                Design.BlockRenderer {
                    width: contentScroll.availableWidth
                    block: blockWindow.snapshot
                    controller: manager.controller
                    detached: true
                    onLinkActivated: (link) => { if (link.startsWith("https://") || link.startsWith("http://")) Qt.openUrlExternally(link) }
                }
            }
        }
        }
    }
}
