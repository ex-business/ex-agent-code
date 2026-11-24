pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Item {
    id: root
    required property Design.SelectableText label
    // 浅色背景用较亮的波峰和短暂停留，让短状态词也能看清扫过方向。
    readonly property color lightCrest: "#d5d5d5"
    property color sweepColor: Design.Theme.dark ? "#eeeeee" : lightCrest
    required property bool running
    readonly property string glyphs: running ? label.selectionText : ""
    // 叠加字形沿用原文实际排版位置；开始选词或退出运行态就释放动画。
    Repeater {
        model: root.running && root.visible && Design.Theme.motionEnabled && !root.label.selectionLength ? Math.min(64, root.glyphs.length) : 0
        Text {
            id: glyph
            required property int index
            readonly property rect placement: root.label.positionToRectangle(index)
            x: placement.x; y: placement.y
            text: root.glyphs.charAt(index)
            font: root.label.font
            color: root.sweepColor
            Accessible.ignored: true
            readonly property int delay: 700 * index / Math.max(1, Math.min(64, root.glyphs.length))
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                PropertyAction { value: 0 }
                PauseAnimation { duration: glyph.delay }
                NumberAnimation { from: 0; to: 1; duration: 220 }
                PauseAnimation { duration: Design.Theme.dark ? 0 : 110 }
                NumberAnimation { from: 1; to: 0; duration: 360 }
                PauseAnimation { duration: (Design.Theme.dark ? 970 : 860) - glyph.delay }
            }
        }
    }
}
