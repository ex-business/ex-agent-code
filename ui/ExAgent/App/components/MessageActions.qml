pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

// 回复操作独立于正文渲染，后续接入能力通过信号扩展，不改 Markdown 或代码组件。
RowLayout {
    id: root
    required property string text
    signal copyRequested(string text)
    property bool pooled: false
    onPooledChanged: if (pooled) resetActions()
    onTextChanged: resetActions()

    function resetActions() {
        copiedTimer.stop()
        if (menuLoader.menu) menuLoader.menu.close()
    }
    function copyReply() {
        copyRequested(text)
        copiedTimer.restart()
    }
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
        onClicked: root.copyReply()
    }
    ReplyAction { enabled: false; opacity: 0.45; iconName: "feedback"; tooltip: "反馈样式展示，尚未连接" }
    ReplyAction { enabled: false; opacity: 0.45; iconName: "speaker"; tooltip: "朗读样式展示，尚未连接" }
    ReplyAction {
        id: more
        iconName: "dots"
        tooltip: "更多回复操作"
        onClicked: menuLoader.active = true
        // 菜单仅在点击时创建，避免长会话为每条回复分配弹层。
        Loader {
            id: menuLoader
            active: false
            readonly property Design.ContextMenu menu: item as Design.ContextMenu
            onLoaded: menu.popup(more, 0, more.height)
            sourceComponent: Design.ContextMenu {
                implicitWidth: 152
                onClosed: menuLoader.active = false
                Design.ContextMenuItem { text: "复制回复"; iconName: "copy"; onTriggered: root.copyReply() }
            }
        }
    }
    Timer { id: copiedTimer; interval: 1800 }
}
