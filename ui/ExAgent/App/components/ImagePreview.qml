pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

// 附件与动态媒体共用查看器；附件地址仍来自既有资源解析，不接受外部进程任意地址。
Popup {
    id: root
    required property ChatController controller
    property var imageInfo: ({})
    parent: Overlay.overlay
    width: Math.min(1100, parent ? parent.width - 48 : 1000)
    height: Math.min(820, parent ? parent.height - 48 : 700)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    modal: true; focus: true; padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function openImage(image) { imageInfo = image; open() }
    background: Rectangle { radius: 12; color: Design.Theme.background; border.color: Design.Theme.border }
    contentItem: Loader {
        active: root.opened
        sourceComponent: Design.MediaViewer { block: ({}); controller: root.controller; attachment: root.imageInfo; onCloseRequested: root.close() }
    }
}
