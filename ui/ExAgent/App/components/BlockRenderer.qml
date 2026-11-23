pragma ComponentBehavior: Bound
import QtQuick
import "BlockGeometry.js" as Geometry
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property var block
    required property ChatController controller
    property string messageId: ""
    property int blockIndex: 0
    property var outerWheel: null
    property bool detached: false
    property bool pooled: false
    signal linkActivated(string link)
    readonly property int rendererKind: Number(block.rendererKind || (block.kind === 1 ? 2 : 1))
    readonly property string blockId: block.blockId || ""
    readonly property var viewState: {
        const version = controller.blockViews.revision
        return controller.blockViews.state(root.blockId)
    }
    readonly property bool layoutReady: !showContent || renderer.status === Loader.Ready && Boolean((renderer.item as Design.BlockRendererContent)?.layoutReady)
    // 诊断分别观察槽位和已绑定到当前消息的渲染器；回收对象的旧内容不计入。
    function hasCurrentRenderer(): bool {
        const item = renderer.item as Design.BlockRendererContent
        return renderer.status === Loader.Ready && item && item.messageId === messageId
                && item.blockIndex === blockIndex && (item.block.blockId || "") === blockId
    }
    readonly property bool mediaContent: rendererKind === 5 || rendererKind === 6
    // 独立窗口只是同一块的另一种视图，所有聊天预览均保留原位，不卸载正文。
    readonly property bool showContent: detached || (viewState.visibility === BlockViewRegistry.Visible && viewState.expansion === BlockViewRegistry.Expanded)
    property bool disclosureActive: false
    property bool retainedCode: false
    readonly property int retainedCodeLimit: 64 * 1024
    readonly property bool cacheableCode: rendererKind === 2 && Number(block.contentMode) !== 2 && (block.text || "").length <= retainedCodeLimit
    onCacheableCodeChanged: if (!cacheableCode) retainedCode = false
    Component.onCompleted: Design.PerformanceTrace.contentPhase(root, 3, 1)
    onBlockIdChanged: {
        Design.PerformanceTrace.contentPhase(root, 3, 5)
        retainedCode = false
        disclosureActive = false
        disclosureReset.stop()
        // 列表复用时按消息身份重建缓存归属，不能把前一条消息的呈现当成新消息缓存。
        Qt.callLater(root.rememberCodeRenderer)
    }
    // 方法归属槽位；回收并销毁槽位时，延迟调用随对象失效。
    function rememberCodeRenderer() {
        if (showContent && renderer.status === Loader.Ready) retainedCode = cacheableCode
    }
    onShowContentChanged: { if (height > 0) { disclosureActive = true; disclosureReset.restart() } }
    Timer { id: disclosureReset; interval: 16; onTriggered: { if (blockDisclosure.running) restart(); else root.disclosureActive = false } }
    implicitHeight: contentLayout.implicitHeight
    clip: true
    Design.DisclosureHeight on implicitHeight { id: blockDisclosure; allowed: root.disclosureActive && !root.showContent && !(root.outerWheel && root.outerWheel.animating) }
    readonly property bool staticCode: rendererKind === 2 && Number(block.contentMode) !== 2 && showContent && !detached
    HoverHandler { id: blockHover }
    Design.BlockHeader {
        id: blockHeader
        anchors.top: parent.top
        width: parent.width
        z: 4
        visible: root.blockId.length > 0 && (root.detached || root.rendererKind !== 1 && !root.mediaContent || !root.showContent)
        compact: root.staticCode
        hovered: blockHover.hovered
        outerWheel: root.outerWheel
        block: root.block; viewState: root.viewState; controller: root.controller; detached: root.detached
    }
    ColumnLayout {
        id: contentLayout
        width: parent.width
        spacing: root.staticCode ? 0 : 5
    Item {
        // 头部始终由固定槽位预留空间；不重挂父对象，避免展开/开窗后布局顺序改变。
        Layout.fillWidth: true
        implicitHeight: root.staticCode ? 0 : 30
        visible: blockHeader.visible
    }
    Button {
        visible: !root.showContent && root.viewState.visibility === BlockViewRegistry.Hidden
        text: "内容已关闭 · 重新打开"
        onClicked: root.controller.blockViews.reopen(root.blockId)
    }
    Label { visible: !root.showContent && root.viewState.placement === BlockViewRegistry.Windowed; text: "内容已在独立窗口打开"; color: Design.Theme.secondary; font.pixelSize: 12 }
    Item {
        Layout.fillWidth: true
        // 异步加载器尚未绑定到本块时，静态代码按真实行数占位，正文优先使用同宽缓存。
        readonly property var content: renderer.item as Design.BlockRendererContent
        Layout.preferredHeight: {
            if (!root.showContent) return 0
            if (root.staticCode && root.block.codeLineCount !== undefined)
                return Geometry.codeHeight(root.block.codeLineCount, Geometry.inlineCodeLimit)
            if (content && content.messageId === root.messageId && content.blockIndex === root.blockIndex && (content.block.blockId || "") === root.blockId && content.implicitHeight > 0) return content.implicitHeight
            if (root.rendererKind === 1 && Number(root.block.contentMode) !== 2) {
                const key = root.messageId + ':' + (root.blockId || root.blockIndex)
                const policy = root.outerWheel?.view?.renderPolicy
                return policy?.heightFor(key, root.width) || Geometry.estimatedProse(root.block.text || "", root.width)
            }
            return content?.implicitHeight ?? 24
        }
        Loader {
            id: renderer
            width: parent.width
            height: (item as Item)?.implicitHeight ?? 24
            // 只保留当前delegate中的静态代码；关闭内容或回收delegate仍会释放，不预建历史消息。
            active: root.showContent || root.disclosureActive || (root.retainedCode && root.viewState.visibility === BlockViewRegistry.Visible)
            visible: root.showContent || root.disclosureActive
            asynchronous: true
            onStatusChanged: Design.PerformanceTrace.contentPhase(renderer, 2, status)
            source: Design.BlockRendererRegistry.sourceFor(root.rendererKind)
            onLoaded: {
                root.retainedCode = root.cacheableCode
                const content = item as Design.BlockRendererContent
                if (!content) return
                content.block = Qt.binding(() => root.block)
                content.controller = Qt.binding(() => root.controller)
                content.messageId = Qt.binding(() => root.messageId)
                content.blockIndex = Qt.binding(() => root.blockIndex)
                content.outerWheel = Qt.binding(() => root.outerWheel)
                content.detached = Qt.binding(() => root.detached)
                content.pooled = Qt.binding(() => root.pooled)
                Design.PerformanceTrace.contentPhase(renderer, 2, 4)
            }
        }
    }
    }
    Design.DisclosureEntrance {
        surface: renderer
        contentKey: root.blockId
        expanded: root.showContent
        contentReady: renderer.status === Loader.Ready
        allowed: !(root.outerWheel && root.outerWheel.animating)
    }
    Connections {
        target: renderer.item
        ignoreUnknownSignals: true
        function onLinkActivated(link: string) { root.linkActivated(link) }
    }
}
