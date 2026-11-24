pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls

Button {
    id: action
    property bool primary: false
    property bool quiet: false
    property bool destructive: false
    transform: Design.PressFeedback { button: action }
    leftPadding: 12; rightPadding: 12; topPadding: 7; bottomPadding: 7
    implicitWidth: Math.max(52, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Design.Theme.dialogButtonHeight
    hoverEnabled: true
    readonly property string caption: {
        const plain = text.replace(/&/g,"")
        return ({Close:Design.UiText.get("关闭"), Cancel:Design.UiText.get("取消"), OK:Design.UiText.get("确定")})[plain] || plain
    }
    Accessible.name: caption
    contentItem: Text {
        text: action.caption; textFormat: Text.PlainText; font: action.font
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        color: !action.enabled ? action.palette.placeholderText : action.primary ? action.palette.window : action.destructive ? Design.Theme.danger : action.palette.windowText
    }
    background: Rectangle {
        radius: 6
        color: action.primary && action.enabled ? (action.destructive ? Design.Theme.danger : action.palette.windowText) : action.quiet && !action.hovered && !action.down ? "transparent" : action.hovered && action.enabled ? action.palette.mid : action.palette.button
        border.color: action.visualFocus ? action.palette.highlight : "transparent"
    }
}
