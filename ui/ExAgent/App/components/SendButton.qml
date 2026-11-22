pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

// 固定几何的独立控件，避免 Controls 内容尺寸或背景层级吞掉语音波形。
Item {
    id: root
    property bool hasText: false
    signal triggered()
    implicitWidth: 30
    implicitHeight: 30
    scale: Design.Theme.motionEnabled && mouse.pressed ? 0.94 : 1
    Behavior on scale { NumberAnimation { duration: Design.Theme.pressDuration; easing.type: Easing.OutCubic } }
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: hasText ? "发送消息" : "语音样式展示，未连接"
    Accessible.onPressAction: triggered()
    Rectangle { anchors.fill: parent; radius: width / 2; color: Design.Theme.dark ? "#eeeeee" : "#000000" }
    Design.Icon { x: 6; y: 6; width: 18; height: 18; name: "arrow"; visible: root.hasText; color: Design.Theme.dark ? "#202020" : "#ffffff" }
    Repeater {
        model: [6, 12, 18, 10, 6]
        Rectangle {
            required property int index
            required property int modelData
            visible: !root.hasText
            x: 6.5 + index * 3.8; y: (root.height - height) / 2
            width: 1.5; height: modelData; radius: 0.75
            color: Design.Theme.dark ? "#202020" : "#ffffff"
        }
    }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.triggered() }
    Keys.onSpacePressed: triggered()
    Keys.onReturnPressed: triggered()
    ToolTip.visible: mouse.containsMouse
    ToolTip.text: root.hasText ? "发送 · Enter" : "语音展示 · 未连接 Agent"
}
