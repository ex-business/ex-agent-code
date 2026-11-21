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
    spacing: 1
    Design.IconButton {
        implicitWidth: 26; implicitHeight: 26
        iconName: copiedTimer.running ? "check" : "copy"
        tooltip: "复制回复"
        iconColor: Design.Theme.muted
        iconSize: 14
        onClicked: { root.copyRequested(root.text); copiedTimer.restart() }
    }
    Design.IconButton { implicitWidth: 26; implicitHeight: 26; iconSize: 14; iconName: "feedback"; tooltip: "反馈样式展示，尚未连接"; iconColor: Design.Theme.muted }
    Design.IconButton { implicitWidth: 26; implicitHeight: 26; iconSize: 14; iconName: "speaker"; tooltip: "朗读样式展示，尚未连接"; iconColor: Design.Theme.muted }
    Design.IconButton { implicitWidth: 26; implicitHeight: 26; iconSize: 14; iconName: "dots"; tooltip: "更多回复操作，尚未连接"; iconColor: Design.Theme.muted }
    Timer { id: copiedTimer; interval: 1800 }
}
