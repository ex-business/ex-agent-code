pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Repeater {
    id: overlay
    required property Design.SelectableText surface
    required property var rules
    model: []
    // 复用或换宽时，表格索引和 TextEdit 可能处于两次不同的布局。等本轮绑定完成后一次读取坐标。
    function refresh() {
        if (!surface || !rules || !rules.length || !surface.visible || surface.width <= 0) {
            if (count) model = []
            return
        }
        const positions = []
        const length = surface.length
        for (const rule of rules) {
            if (rule.end < 0 || rule.next < rule.end || rule.next > length) continue
            const end = surface.positionToRectangle(rule.end)
            const next = rule.next === rule.end ? end : surface.positionToRectangle(rule.next)
            if (end.height > 0 && next.height > 0)
                positions.push(Math.max(end.y + end.height + 3, (end.y + end.height + next.y) / 2))
        }
        model = positions
    }
    function schedule() {
        if (rules?.length || count) Qt.callLater(overlay.refresh)
    }
    onRulesChanged: schedule()
    property Connections layoutSignals: Connections {
        target: overlay.surface
        function onWidthChanged() { overlay.schedule() }
        function onImplicitHeightChanged() { overlay.schedule() }
        function onLengthChanged() { overlay.schedule() }
        function onVisibleChanged() { overlay.schedule() }
    }
    delegate: Rectangle {
        required property real modelData
        y: modelData
        width: overlay.surface.width
        height: 1
        color: Design.Theme.border
    }
}
