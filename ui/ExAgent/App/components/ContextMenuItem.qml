pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

MenuItem {
    id: root
    implicitHeight: 33
    leftPadding: 11
    rightPadding: 11
    contentItem: Text {
        text: root.text
        color: root.enabled ? Design.Theme.text : Design.Theme.muted
        opacity: root.enabled ? 1 : 0.65
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 5
        color: root.highlighted ? Design.Theme.hover : "transparent"
    }
}
