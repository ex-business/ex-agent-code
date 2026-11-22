pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

AbstractButton {
    id: control
    property bool inverted: false
    implicitHeight: 32
    implicitWidth: label.implicitWidth + 22
    transform: Design.PressFeedback { button: control }
    hoverEnabled: true
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
    contentItem: Text { id: label; text: control.text; font.pixelSize: 12; color: control.inverted ? (control.enabled ? "#eeeeee" : "#777777") : control.enabled ? Design.Theme.text : Design.Theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    background: Rectangle { radius: 7; color: control.inverted ? (control.hovered || control.down || control.activeFocus ? "#484848" : "transparent") : control.down ? Design.Theme.selected : control.hovered || control.activeFocus ? Design.Theme.hover : "transparent"; border.color: control.activeFocus ? Design.Theme.secondary : "transparent" }
    HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: control.enabled }
}
