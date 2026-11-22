pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

ColumnLayout {
    id: root
    required property var block
    required property ChatController controller
    property string messageId: ""
    property int blockIndex: 0
    property var outerWheel: null
    property bool detached: false
    signal linkActivated(string link)
    readonly property int rendererKind: Number(block.rendererKind || (block.kind === 1 ? 2 : 1))
    readonly property string blockId: block.blockId || ""
    readonly property var viewState: {
        const version = controller.blockViews.revision
        return controller.blockViews.state(root.blockId)
    }
    readonly property bool mediaContent: rendererKind === 5 || rendererKind === 6
    // 媒体窗口打开后保留预览原位，避免后续聊天跳动；内嵌预览不播放。
    readonly property bool showContent: detached || (viewState.visibility === 1 && viewState.expansion === 1 && (viewState.placement === 1 || mediaContent))
    spacing: 5
    // 普通正文不逐段展示管理按钮；只有独立内容卡片保留一行明确的文字操作。
    component ActionLink: ToolButton {
        id: actionLink
        implicitHeight: 22
        implicitWidth: label.implicitWidth + 10
        padding: 0
        hoverEnabled: true
        contentItem: Text {
            id: label
            text: actionLink.text
            font.pixelSize: 11
            color: actionLink.hovered || actionLink.activeFocus ? Design.Theme.text : Design.Theme.secondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.underline: actionLink.hovered
        }
        background: Item { }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 5
        visible: root.blockId.length > 0 && (root.detached || (root.rendererKind !== 1 && root.rendererKind !== 5 && root.rendererKind !== 6) || (Number(root.block.contentMode) === 2 && root.rendererKind !== 5 && root.rendererKind !== 6) || !root.showContent)
        Layout.preferredHeight: 22
        Label {
            Layout.fillWidth: true
            text: root.block.title || Design.BlockRendererRegistry.labelFor(root.rendererKind)
            color: Design.Theme.muted
            elide: Text.ElideRight
            font.pixelSize: 11
            DragHandler {
                enabled: !root.detached
                target: null
                onActiveChanged: if (active) root.controller.blockViews.detach(root.block)
            }
        }
        ActionLink { text: root.viewState.expansion === 2 ? "展开" : "收起"; visible: !root.detached && root.viewState.placement === 1; onClicked: root.controller.blockViews.setExpanded(root.blockId, root.viewState.expansion === 1 ? 2 : 1) }
        ActionLink {
            text: root.detached || root.viewState.placement === 2 ? "放回聊天" : "打开窗口"
            onClicked: {
                if (root.detached || root.viewState.placement === 2) root.controller.blockViews.reattach(root.blockId)
                else root.controller.blockViews.detach(root.block)
            }
        }
        ActionLink { text: "导出"; onClicked: root.controller.requestBlockExport(root.block) }
        ActionLink { text: "关闭"; visible: !root.detached && root.viewState.placement === 1; onClicked: root.controller.blockViews.closeInline(root.blockId) }
    }
    Button {
        visible: !root.showContent && root.viewState.visibility === 2
        text: "内容已关闭 · 重新打开"
        onClicked: root.controller.blockViews.reopen(root.blockId)
    }
    Label { visible: !root.showContent && root.viewState.placement === 2; text: "内容已在独立窗口打开"; color: Design.Theme.secondary; font.pixelSize: 12 }
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: root.showContent ? ((renderer.item as Item)?.implicitHeight ?? 24) : 0
        Loader {
            id: renderer
            anchors.fill: parent
            active: root.showContent
            asynchronous: true
            source: Design.BlockRendererRegistry.sourceFor(root.rendererKind)
            onLoaded: {
                const content = item as Design.BlockRendererContent
                if (!content) return
                content.block = Qt.binding(() => root.block)
                content.controller = Qt.binding(() => root.controller)
                content.messageId = Qt.binding(() => root.messageId)
                content.blockIndex = Qt.binding(() => root.blockIndex)
                content.outerWheel = Qt.binding(() => root.outerWheel)
                content.detached = Qt.binding(() => root.detached)
            }
        }
    }
    Connections {
        target: renderer.item
        ignoreUnknownSignals: true
        function onLinkActivated(link: string) { root.linkActivated(link) }
    }
}
