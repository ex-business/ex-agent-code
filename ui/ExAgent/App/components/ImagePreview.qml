pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

// 全窗口灯箱不参与聊天布局，关闭后列表锚点和选区仍由原视图持有。
Popup {
    id: root
    required property ChatController controller
    property var imageInfo: ({})
    parent: Overlay.overlay
    width: parent ? parent.width : 1000
    height: parent ? parent.height : 700
    modal: true; focus: true; padding: 0
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Design.Theme.disclosureDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Design.Theme.pressDuration } }
    closePolicy: Popup.CloseOnEscape
    function openImage(image) { imageInfo = image; open() }
    background: Rectangle { color: "#df181818" }
    contentItem: Loader {
        active: root.visible
        sourceComponent: Design.MediaViewer { block: ({}); overlayMode: true; controller: root.controller; attachment: root.imageInfo; onCloseRequested: root.close() }
    }
}
