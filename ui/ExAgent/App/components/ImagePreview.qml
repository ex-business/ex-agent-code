pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import ExAgent.App as Design

Popup {
    id: root
    property var imageInfo: ({})
    property real zoom: 1
    parent: Overlay.overlay
    width: Math.min(1100, parent ? parent.width - 48 : 1000)
    height: Math.min(820, parent ? parent.height - 48 : 700)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    modal: true
    focus: true
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function openImage(image) { imageInfo = image; zoom = 1; open() }
    background: Rectangle { radius: 14; color: Design.Theme.background; border.color: Design.Theme.border }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.imageInfo.name || "图像"; Layout.fillWidth: true; elide: Text.ElideMiddle; color: Design.Theme.text; font.pixelSize: 13 }
            Text { text: (root.imageInfo.width || 0) + " × " + (root.imageInfo.height || 0); color: Design.Theme.muted; font.pixelSize: 11 }
            Design.IconButton { iconName: "minus"; tooltip: "缩小图像"; onClicked: root.zoom = Math.max(0.5, root.zoom / 1.25) }
            Button {
                text: "适应窗口"
                flat: true
                onClicked: root.zoom = 1
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            Design.IconButton { iconName: "plus"; tooltip: "放大图像"; onClicked: root.zoom = Math.min(8, root.zoom * 1.25) }
            Design.IconButton { iconName: "close"; tooltip: "关闭图像预览"; onClicked: root.close() }
        }
        Flickable {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, original.width)
            contentHeight: Math.max(height, original.height)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            Image {
                id: original
                readonly property real fit: Math.min(1, viewport.width / Math.max(1, root.imageInfo.width || 1), viewport.height / Math.max(1, root.imageInfo.height || 1))
                width: (root.imageInfo.width || 1) * fit * root.zoom
                height: (root.imageInfo.height || 1) * fit * root.zoom
                x: Math.max(0, (viewport.width - width) / 2)
                y: Math.max(0, (viewport.height - height) / 2)
                source: root.imageInfo.url || ""
                sourceSize.width: Math.min(8192, Math.ceil(width * Screen.devicePixelRatio))
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                smooth: true
                Accessible.name: "原图 " + (root.imageInfo.name || "")
            }
        }
    }
}
