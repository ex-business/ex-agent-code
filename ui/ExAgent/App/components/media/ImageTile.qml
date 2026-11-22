pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import ExAgent.App as Design

// 附件和动态预览共享缩略图，不在聊天内解码原图；布局尺寸由元数据决定。
Rectangle {
    id: tile
    property url source
    property string label: "图片"
    property bool compact: false
    property int count: 1
    signal activated()
    radius: 12
    color: Design.Theme.dark ? "#252525" : "#f5f5f6"
    border.color: pointer.containsMouse || activeFocus ? Design.Theme.secondary : Design.Theme.border
    clip: true
    scale: Design.Theme.motionEnabled && pointer.pressed ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Design.Theme.pressDuration; easing.type: Easing.OutCubic } }
    activeFocusOnTab: true
    Image {
        id: thumbnail
        anchors.fill: parent; anchors.margins: 5
        source: tile.source
        // 缓存的是受sourceSize约束的小缩略图，复用消息时避免重新解码原文件。
        asynchronous: true; cache: true
        sourceSize: Qt.size(Math.ceil(tile.width * Screen.devicePixelRatio), Math.ceil(tile.height * Screen.devicePixelRatio))
        fillMode: Image.PreserveAspectFit
    }
    Text { anchors.centerIn: parent; visible: thumbnail.status !== Image.Ready; text: thumbnail.status === Image.Error ? "图片不可用" : "加载中…"; color: Design.Theme.muted; font.pixelSize: 12 }
    Rectangle {
        visible: tile.count > 1
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9
        width: badge.implicitWidth + 14; height: 23; radius: 11
        color: "#bd202020"
        Text { id: badge; anchors.centerIn: parent; text: tile.count + " 张"; color: "white"; font.pixelSize: 11 }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button; Accessible.name: "查看图片 " + tile.label
        Accessible.onPressAction: tile.activated()
        onClicked: tile.activated()
        ToolTip.visible: containsMouse; ToolTip.delay: 600; ToolTip.text: tile.label
    }
    Keys.onReturnPressed: activated()
    Keys.onSpacePressed: activated()
}
