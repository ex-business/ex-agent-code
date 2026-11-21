pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

Item {
    id: root
    required property Flickable view
    required property int messageCount
    required property int loadedCount
    property real currentFraction: 1
    signal seekRequested(real fraction)
    implicitWidth: 25
    readonly property int marks: Math.max(1, Math.min(96, messageCount))
    Accessible.role: Accessible.Slider
    Accessible.name: "聊天定位刻度"
    Column {
        anchors.fill: parent
        Repeater {
            model: root.marks
            delegate: Item {
                id: mark
                required property int index
                width: root.width
                height: root.height / root.marks
                Rectangle {
                    anchors.centerIn: parent
                    width: mark.index === Math.round(root.currentFraction * (root.marks - 1)) ? 11 : 6
                    height: 2
                    radius: 1
                    color: mark.index === Math.round(root.currentFraction * (root.marks - 1)) ? Design.Theme.text : Design.Theme.dark ? "#555555" : "#d5d7da"
                }
            }
        }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        function seek(y: real) { root.seekRequested(Math.max(0, Math.min(1, y / height))) }
        onPressed: (mouse) => seek(mouse.y)
        onPositionChanged: (mouse) => { if (pressed) seek(mouse.y) }
        ToolTip.visible: containsMouse
        ToolTip.delay: 400
        ToolTip.text: "第 " + Math.max(1, Math.round(root.currentFraction * root.messageCount)) + " / " + root.messageCount + " 条 · 点击定位"
    }
}
