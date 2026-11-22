pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import ExAgent.App as Design

// 只缩放控件自身，不改变布局尺寸或鼠标命中区域；文本选区不参与按钮动效。
Scale {
    id: feedback
    required property T.AbstractButton button
    property real factor: Design.Theme.motionEnabled && button.down ? 0.94 : 1
    origin.x: button.width / 2
    origin.y: button.height / 2
    xScale: factor
    yScale: factor
    Behavior on factor { NumberAnimation { duration: Design.Theme.pressDuration; easing.type: Easing.OutCubic } }
}
