pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

AbstractButton {
    id: root
    property string iconName: "plus"
    property string tooltip: ""
    property color iconColor: Design.Theme.secondary
    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    Accessible.name: tooltip
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 500
    background: Rectangle {
        radius: 7
        color: root.down ? Design.Theme.selected : root.hovered ? Design.Theme.hover : "transparent"
    }
    contentItem: Item {
        Design.Icon { anchors.centerIn: parent; name: root.iconName; color: root.iconColor; implicitWidth: 17; implicitHeight: 17 }
    }
}
