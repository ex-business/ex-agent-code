pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

AbstractButton {
    id: control
    implicitHeight: 32
    implicitWidth: label.implicitWidth + 22
    hoverEnabled: true
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
    contentItem: Text { id: label; text: control.text; font.pixelSize: 12; color: control.enabled ? Design.Theme.text : Design.Theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    background: Rectangle { radius: 7; color: control.down ? Design.Theme.selected : control.hovered || control.activeFocus ? Design.Theme.hover : "transparent"; border.color: control.activeFocus ? Design.Theme.secondary : "transparent" }
    HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: control.enabled }
}
