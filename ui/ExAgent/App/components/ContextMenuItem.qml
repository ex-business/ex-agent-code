pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

MenuItem {
    id: root
    property string iconName: ""
    property string shortcutLabel: ""
    property bool destructive: false
    implicitHeight: visible ? 36 : 0
    leftPadding: 10; rightPadding: 10
    transform: Design.PressFeedback { button: root }
    hoverEnabled: true
    Accessible.name: text
    contentItem: RowLayout {
        spacing: 10
        Design.Icon { name: root.iconName; opacity: root.iconName.length > 0 ? (root.enabled ? 1 : 0.4) : 0; implicitWidth: 17; implicitHeight: 17; color: root.destructive ? Design.Theme.danger : Design.Theme.secondary }
        Text { text: root.text; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13; color: root.destructive ? Design.Theme.danger : root.enabled ? Design.Theme.text : Design.Theme.muted; opacity: root.enabled ? 1 : 0.6 }
        Text { text: root.shortcutLabel; visible: text.length > 0; color: Design.Theme.muted; font.pixelSize: 11 }
    }
    background: Rectangle { radius: 7; color: root.highlighted ? Design.Theme.hover : "transparent" }
}
