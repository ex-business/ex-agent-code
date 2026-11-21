pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import ExAgent.App as Design

Flickable {
    id: root
    required property var attachments
    signal imageRequested(var image)
    implicitHeight: attachments.length > 0 ? 84 : 0
    contentWidth: thumbnails.implicitWidth
    contentHeight: height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick
    Row {
        id: thumbnails
        spacing: 8
        Repeater {
            model: root.attachments
            delegate: Rectangle {
                id: thumbnail
                required property var modelData
                width: 80; height: 80; radius: 10
                border.color: Design.Theme.border
                color: Design.Theme.background
                Image {
                    anchors.fill: parent; anchors.margins: 3
                    source: thumbnail.modelData.url
                    sourceSize.width: Math.ceil(80 * Screen.devicePixelRatio)
                    sourceSize.height: Math.ceil(80 * Screen.devicePixelRatio)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    smooth: true
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "查看原图 " + thumbnail.modelData.name
                    Accessible.onPressAction: root.imageRequested(thumbnail.modelData)
                    onClicked: root.imageRequested(thumbnail.modelData)
                    ToolTip.visible: containsMouse
                    ToolTip.text: thumbnail.modelData.name
                }
            }
        }
    }
    ScrollBar.horizontal: ScrollBar { height: 3; policy: ScrollBar.AsNeeded }
}
