pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

// 回复操作独立于正文渲染，后续接入能力通过信号扩展，不改 Markdown 或代码组件。
RowLayout {
    id: root
    required property string text
    signal copyRequested(string text)
    onTextChanged: copiedTimer.stop()
    spacing: 2
    // 消息操作统一使用18px图形和32px命中区域，便于辨认与点击。
    component ReplyAction: Design.IconButton {
        implicitWidth: 32
        implicitHeight: 32
        iconSize: 18
        iconColor: Design.Theme.secondary
    }
    ReplyAction {
        iconName: copiedTimer.running ? "check" : "copy"
        tooltip: "复制回复"
        onClicked: { root.copyRequested(root.text); copiedTimer.restart() }
    }
    ReplyAction { iconName: "feedback"; tooltip: "反馈样式展示，尚未连接" }
    ReplyAction { iconName: "speaker"; tooltip: "朗读样式展示，尚未连接" }
    ReplyAction { iconName: "dots"; tooltip: "更多回复操作，尚未连接" }
    Timer { id: copiedTimer; interval: 1800 }
}
