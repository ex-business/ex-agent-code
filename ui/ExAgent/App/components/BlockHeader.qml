pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

// 块操作集中在明确命名的菜单；只改视图，不取消后台任务。
Item {
    id: header
    required property var block
    required property var viewState
    required property ChatController controller
    property var outerWheel: null
    function prepareDisclosure() { if (outerWheel) outerWheel.view.beginContentDisclosure() }
    function toggleExpanded() {
        prepareDisclosure()
        controller.blockViews.setExpanded(block.blockId, expanded ? BlockViewRegistry.Collapsed : BlockViewRegistry.Expanded)
    }
    property bool compact: false
    property bool detached: false
    property bool hovered: false
    readonly property bool expanded: viewState.expansion === BlockViewRegistry.Expanded
    implicitHeight: compact ? 38 : 30
    RowLayout {
        anchors.left: header.compact ? undefined : parent.left
        anchors.right: parent.right
        anchors.rightMargin: header.compact ? 110 : 0
        y: header.compact ? 3 : 0
        height: 30
        spacing: 8
        opacity: !header.compact || header.hovered || actions.activeFocus || menu.opened ? 1 : 0
        Text {
            visible: !header.compact
            Layout.fillWidth: true; elide: Text.ElideRight
            text: header.block.title || Design.BlockRendererRegistry.labelFor(Number(header.block.rendererKind))
            color: Design.Theme.secondary; font.pixelSize: 12
            DragHandler { target: null; enabled: !header.detached; onActiveChanged: if (active) header.controller.blockViews.detach(header.block) }
        }
        Design.IconButton {
            visible: !header.compact && !header.detached && header.viewState.placement === BlockViewRegistry.Inline
            iconName: header.expanded ? "down" : "right"
            tooltip: header.expanded ? "收起内容" : "展开内容"
            implicitWidth: 28; implicitHeight: 28
            onClicked: header.toggleExpanded()
        }
        ToolButton {
            id: actions
            transform: Design.PressFeedback { button: actions }
            text: "操作"
            implicitWidth: 50; implicitHeight: 28
            hoverEnabled: true
            Accessible.name: "内容操作"
            contentItem: RowLayout { spacing: 3; Text { text: actions.text; color: Design.Theme.secondary; font.pixelSize: 12 } Design.Icon { name: "down"; implicitWidth: 11; implicitHeight: 11 } }
            background: Rectangle { radius: 7; color: actions.hovered || actions.activeFocus || menu.opened ? Design.Theme.hover : "transparent" }
            onClicked: menu.popup(actions, Math.min(0, actions.width - menu.width), actions.height + 3)
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }
    Design.ContextMenu {
        id: menu
        Design.ContextMenuItem { text: header.expanded ? "收起内容" : "展开内容"; iconName: header.expanded ? "minus" : "plus"; visible: !header.detached && header.viewState.placement === BlockViewRegistry.Inline; onTriggered: header.toggleExpanded() }
        Design.ContextMenuItem { text: header.detached ? "放回聊天" : header.viewState.placement === BlockViewRegistry.Windowed ? "显示独立窗口" : "在独立窗口打开"; iconName: "expand"; onTriggered: { if (header.detached) header.controller.blockViews.reattach(header.block.blockId); else header.controller.blockViews.detach(header.block) } }
        Design.ContextMenuItem { text: "放回聊天"; iconName: "back"; visible: !header.detached && header.viewState.placement === BlockViewRegistry.Windowed; onTriggered: header.controller.blockViews.reattach(header.block.blockId) }
        Design.ContextMenuItem { text: "导出内容数据…"; iconName: "download"; onTriggered: header.controller.requestBlockExport(header.block) }
        MenuSeparator { }
        Design.ContextMenuItem { text: "关闭此内容"; iconName: "close"; destructive: true; visible: !header.detached && header.viewState.placement === BlockViewRegistry.Inline; onTriggered: { header.prepareDisclosure(); header.controller.blockViews.closeInline(header.block.blockId) } }
    }
}
