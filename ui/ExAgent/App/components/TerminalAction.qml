pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

AbstractButton {
    id: button
    property string glyph
    property bool primary: false
    property bool darkChrome: false
    hoverEnabled: true
    padding: 7
    leftPadding: 9
    rightPadding: 9
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: 30
    opacity: enabled ? 1 : 0.48
    background: Rectangle {
        radius: 7
        color: button.darkChrome ? (button.primary ? "#dbe6f3" : button.checked ? "#334152" : button.hovered || button.down ? "#29313e" : "transparent") : button.primary && !button.hovered ? Design.Theme.surface : button.checked || button.down ? Design.Theme.selected : button.hovered ? Design.Theme.hover : "transparent"
        border.width: button.visualFocus || button.primary ? 1 : 0
        border.color: button.darkChrome ? (button.visualFocus ? "#96badc" : "#43505f") : button.visualFocus ? Design.Theme.secondary : Design.Theme.border
    }
    contentItem: RowLayout {
        spacing: 6
        Design.Icon { name: button.glyph; implicitWidth: 15; implicitHeight: 15; color: label.color }
        Text { id: label; text: button.text; font.family: Design.Theme.font; font.pixelSize: 12
            color: button.darkChrome ? (button.primary ? "#172331" : button.checked ? "#eff5ff" : "#bec8d6") : Design.Theme.text }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
