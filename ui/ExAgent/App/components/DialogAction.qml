pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls

Button {
    id: action
    property bool primary: false
    leftPadding: 12; rightPadding: 12; topPadding: 6; bottomPadding: 6
    implicitWidth: Math.max(52, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 32
    hoverEnabled: true
    readonly property string caption: {
        const plain = text.replace(/&/g,"")
        return ({Close:Design.UiText.get("关闭"), Cancel:Design.UiText.get("取消"), OK:Design.UiText.get("确定")})[plain] || plain
    }
    Accessible.name: caption
    contentItem: Text {
        text: action.caption; font: action.font
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        color: !action.enabled ? action.palette.placeholderText : action.primary ? action.palette.window : action.palette.windowText
    }
    background: Rectangle {
        radius: 6
        color: action.primary && action.enabled ? action.palette.windowText : action.hovered && action.enabled ? action.palette.mid : action.palette.button
        border.color: action.visualFocus ? action.palette.highlight : "transparent"
    }
}
