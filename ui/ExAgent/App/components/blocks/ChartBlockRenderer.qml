pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    readonly property var payloadData: root.block.data || ({})
    readonly property var series: Design.BlockRendererRegistry.values(payloadData.series, 64)
    readonly property real maximum: Math.max(1, ...series.map(point => Math.max(0, Number(point.value) || 0)))
    implicitHeight: series.length > 0 ? 230 : pending.implicitHeight + 20
    Rectangle { anchors.fill: parent; radius: 12; color: Design.Theme.code; border.color: Design.Theme.border }
    Text { id: pending; visible: root.series.length === 0; x: 12; y: 10; width: parent.width - 24; text: root.block.text || "暂无图表数据"; wrapMode: Text.Wrap; color: Design.Theme.secondary }
    Row {
        visible: root.series.length > 0
        x: 20; y: 20; width: parent.width - 40; height: 170
        spacing: 3
        Repeater {
            model: root.series
            Item {
                required property var modelData
                required property int index
                width: Math.max(2, ((root.width - 40) / Math.max(1, root.series.length)) - 3)
                height: 170
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(1, Math.max(0, Number(parent.modelData.value) || 0) / root.maximum * 145)
                    radius: 2
                    color: hover.hovered ? "#4d88cc" : "#649ad6"
                }
                HoverHandler { id: hover }
                Text {
                    visible: hover.hovered
                    anchors.bottom: parent.top
                    text: (parent.modelData.label || "") + ": " + parent.modelData.value
                    color: Design.Theme.text
                    font.pixelSize: 11
                }
            }
        }
    }
    Text { visible: root.series.length > 0; x: 20; y: 204; text: "前 " + root.series.length + " 条消息 · " + (root.payloadData.unit || "数值"); color: Design.Theme.secondary; font.pixelSize: 12 }
}
