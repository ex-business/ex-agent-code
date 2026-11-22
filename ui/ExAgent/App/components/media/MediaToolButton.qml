pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

// 灯箱图标使用独立的高对比样式，命中区域固定，不受图片颜色影响。
AbstractButton {
    id: control
    property string iconName: "close"
    property string tooltip: ""
    implicitWidth: 36; implicitHeight: 36
    transform: Design.PressFeedback { button: control }
    hoverEnabled: true
    Accessible.name: tooltip
    ToolTip.visible: hovered; ToolTip.text: tooltip; ToolTip.delay: 450
    contentItem: Item { Design.Icon { anchors.centerIn: parent; name: control.iconName; width: 19; height: 19; color: control.enabled ? "#ffffff" : "#777777" } }
    background: Rectangle { radius: 18; color: control.down ? "#696969" : control.hovered || control.activeFocus ? "#555555" : "#363636"; border.color: control.activeFocus ? "#ffffff" : "#555555" }
    HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: control.enabled }
}
