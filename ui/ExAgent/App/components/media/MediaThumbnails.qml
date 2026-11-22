pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

// 横向ListView只创建视口附近的缩略图，长序列不一次解码所有帧。
ListView {
    id: strip
    required property Design.MediaSession session
    implicitHeight: 72
    orientation: ListView.Horizontal
    spacing: 8; clip: true; cacheBuffer: 160; reuseItems: true
    model: session.frames
    currentIndex: session.frameIndex
    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
    delegate: Rectangle {
        id: tile
        required property var modelData
        required property int index
        width: 76; height: 66; radius: 7
        color: Design.Theme.surface
        border.width: strip.session.frameIndex === index ? 2 : 1
        border.color: strip.session.frameIndex === index ? Design.Theme.text : Design.Theme.border
        Image { anchors.fill: parent; anchors.margins: 4; anchors.bottomMargin: 16; source: strip.session.sourceForFrame(tile.index); sourceSize: Qt.size(144, 96); asynchronous: true; cache: false; fillMode: Image.PreserveAspectFit }
        Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: tile.index + 1; color: Design.Theme.secondary; font.pixelSize: 10 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; Accessible.role: Accessible.Button; Accessible.name: "跳到第" + (tile.index + 1) + "张"; Accessible.onPressAction: strip.session.seek(tile.index); onClicked: strip.session.seek(tile.index) }
    }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
}
