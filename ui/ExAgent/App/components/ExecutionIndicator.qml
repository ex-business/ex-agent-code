pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Item {
    id: root
    required property Design.SelectableText label
    property color sweepColor: Design.Theme.dark ? "#eeeeee" : "#adadad"
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
                NumberAnimation { from: 1; to: 0; duration: 360 }
                PauseAnimation { duration: 970 - glyph.delay }
            }
        }
    }
}
