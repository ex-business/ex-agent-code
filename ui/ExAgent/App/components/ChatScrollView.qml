pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQml.Models
import ExAgent.Chat
import ExAgent.App as Design

ListView {
    id: transcript
    required property ChatController controller
    signal fileRequested(string path)
    signal imageRequested(var image)
    readonly property alias selectionDocument: selectionLayer.document
    readonly property alias selectionManager: selectionLayer
    function stop() { transcriptWheel.stop() }
    property var layoutAnchor: null
    function resetForSession() {
        renderPolicy.reset()
        layoutAnchor = null; pinnedMessage = ""; pinLayout.stop(); groupDisclosure.stop(); disclosureGroupId = ""
        waitingOlder = false
        jumpPage.stop()
        jumpTarget = -1
        stop()
        selectionLayer.finish()
        revealLatest()
    }
    Design.ChatSelectionLayer {
        id: selectionLayer
        view: transcript
        model: transcript.controller.messages
        onOlderPageRequested: transcript.loadPreviousPage()
        onLayoutChangeStarting: transcript.captureLayoutAnchor()
        onLayoutChangeFinished: Qt.callLater(transcript.restoreLayoutAnchor)
    }
    function captureLayoutAnchor() {
        if (followingEnd || layoutAnchor || loadingOlder || moving || renderPolicy.scrolling) return
        // 顶部可能落在消息间距或折叠行上；按实际相交的可见消息找锚点，不能因此放弃位置保持。
        let message = null
        for (const child of contentItem.children) {
            const row = child as MessageRow
            if (!row || row.height <= 0 || row.y + row.height <= contentY || row.y >= contentY + height) continue
            if (!message || row.y < message.y) message = row
        }
        if (message) layoutAnchor = {id: message.messageId, offset: contentY - message.y}
    }
    property string disclosureGroupId: ""
    function beginContentDisclosure() {
        // 主动折叠时退出贴底，不能在每帧高度变化时再次拉动整页。
        transcriptWheel.stop()
        followingEnd = false
    }
    function beginGroupDisclosure(id: string) { beginContentDisclosure(); disclosureGroupId = id; groupDisclosure.restart() }
    Timer {
        id: groupDisclosure
        interval: 16
        onTriggered: {
            // 等待实际动画完成，再卸载内容与复核锚点，避免残留空白。
            for (const child of transcript.contentItem.children) {
                if (child instanceof MessageRow && child.disclosureAnimating) { restart(); return }
            }
            transcript.disclosureGroupId = ""
            transcript.settlePinnedMessage()
        }
    }
    property string pinnedMessage: ""
    property int pinPass: 0
    function pinMessageAnchor(id: string, viewportY: real) {
        followingEnd = false
        layoutAnchor = {id: id, offset: -viewportY}
        pinnedMessage = id
        pinPass = 0
    }
    function settlePinnedMessage() { if (!groupDisclosure.running) pinLayout.restart() }
    Timer {
        id: pinLayout
        interval: 16
        repeat: true
        onTriggered: {
            // ListView会分批修正回收行的高度；短暂复核，保证折叠后的标题仍可见。
            const row = transcript.selectionDocument.orderOf(transcript.pinnedMessage)
            if (row < 0) { stop(); return }
            transcript.forceLayout()
            transcript.positionViewAtIndex(row, ListView.Contain)
            if (++transcript.pinPass >= 3) { stop(); transcript.pinnedMessage = "" }
        }
    }
    function restoreLayoutAnchor() {
        const anchor = layoutAnchor; layoutAnchor = null
        if (!anchor || followingEnd || loadingOlder || moving || renderPolicy.scrolling) return
        renderPolicy.adjustingLayout = true
        try {
            forceLayout()
            const row = selectionDocument.orderOf(anchor.id)
            if (row >= 0 && !itemAtIndex(row)) positionViewAtIndex(row, ListView.Beginning)
            const item = row >= 0 ? itemAtIndex(row) : null
            if (item) {
                const before = contentY
                contentY = Math.max(originY - topMargin, Math.min(Math.max(originY - topMargin, originY + contentHeight - height + bottomMargin), item.y + anchor.offset))
                Design.PerformanceTrace.layoutAnchor(contentY - before, contentY - item.y - anchor.offset)
            }
        } finally { renderPolicy.adjustingLayout = false }
    }
    objectName: "messageList"
    property bool pixelWheelEnabled: true
    property bool pixelWheelAnimated: Design.Theme.motionEnabled
    readonly property bool pixelWheelMoving: transcriptWheel.kineticRunning
    function pixelScrollBy(x: real, y: real): point { return Qt.point(0, transcriptWheel.consumePixels(y)) }
    signal pixelScrollStarted()
    readonly property int prefetchDirection: renderPolicy.direction
    readonly property real prefetchSpeed: renderPolicy.speed
    readonly property int prefetchAhead: renderPolicy.aheadPixels
    readonly property int prefetchBehind: renderPolicy.behindPixels
    readonly property alias fastScrolling: renderPolicy.fast
    readonly property alias renderPolicy: renderPolicy
    Design.ScrollRenderPolicy { id: renderPolicy; view: transcript }
    anchors.fill: parent
    visible: transcript.controller.messages.count > 0
    clip: true
    pixelAligned: true
    reuseItems: true
    cacheBuffer: renderPolicy.preloadPixels
    spacing: 5
    topMargin: 24
    bottomMargin: 15
    boundsBehavior: Flickable.DragAndOvershootBounds
    acceptedButtons: Qt.NoButton
    model: transcript.controller.messages
    property bool followingEnd: true
    property bool loadingOlder: false
    property bool waitingOlder: false
    function prefetchInTravelDirection() {
        // 仅前往历史方向时读下一页，反向不追加预读；数据层仍保持单页缓存和代次取消。
        if (renderPolicy.direction < 0 && contentY - originY < renderPolicy.aheadPixels && controller.messages.hasOlder)
            controller.messages.prefetchOlder()
    }
    onContentYChanged: prefetchInTravelDirection()
    Connections {
        target: renderPolicy
        function onDirectionChanged() { transcript.prefetchInTravelDirection() }
        function onAheadPixelsChanged() { transcript.prefetchInTravelDirection() }
    }
    Connections {
        target: transcript.controller.messages
        function onPageFailure(error: string) { transcript.waitingOlder = false; transcript.jumpTarget = -1 }
        function onOlderPageReady() {
            if (transcript.jumpTarget >= 0) jumpPage.restart()
            else if (transcript.waitingOlder) { transcript.waitingOlder = false; transcript.loadPreviousPage() }
        }
    }
    property int jumpTarget: -1
    function seekFraction(fraction: real) {
        transcriptWheel.stop()
        followingEnd = false
        jumpTarget = Math.round(fraction * Math.max(0, transcript.controller.messages.totalCount - 1))
        jumpPage.restart()
    }
    Timer {
        id: jumpPage
        interval: 16
        onTriggered: {
            const first = transcript.controller.messages.totalCount - transcript.controller.messages.count
            if (transcript.jumpTarget < first && transcript.controller.messages.hasOlder) {
                if (transcript.controller.messages.loadOlder() > 0) { restart(); return }
                if (transcript.controller.messages.olderLoading) return
            }
            transcript.forceLayout()
            transcript.positionViewAtIndex(Math.max(0, transcript.jumpTarget - (transcript.controller.messages.totalCount - transcript.controller.messages.count)), ListView.Beginning)
            transcript.jumpTarget = -1
        }
    }
    Design.SmoothWheel {
        id: transcriptWheel
        view: transcript
        selectionManager: selectionLayer
        onScrollingStarted: { renderPolicy.beginScroll(); transcript.layoutAnchor = null; transcript.pinnedMessage = ""; pinLayout.stop(); groupDisclosure.stop(); transcript.disclosureGroupId = ""; transcript.followingEnd = false }
        onScrollingSettled: {
            if (transcriptScrollBar.pressed || selectionLayer.origin) return
            if (transcript.atYEnd) transcript.followingEnd = true
            if (transcript.contentY - transcript.originY < 120) transcript.loadPreviousPage()
        }
    }
    function loadPreviousPage(retry = false) {
        if (loadingOlder || jumpTarget >= 0 || transcriptScrollBar.pressed || transcriptWheel.animating || !transcript.controller.messages.hasOlder) return
        if (!transcript.controller.messages.olderReady) {
            waitingOlder = true
            // 仅用户明确点击时清除读取失败标记，自动滚动不循环重试损坏资源。
            if (retry) transcript.controller.messages.loadOlder()
            else transcript.controller.messages.prefetchOlder()
            return
        }
        loadingOlder = true
        followingEnd = false
        const row = Math.max(0, indexAt(width / 2, contentY + 2))
        const item = itemAtIndex(row)
        const inset = item ? contentY - item.y : 0
        const added = transcript.controller.messages.loadOlder()
        forceLayout()
        positionViewAtIndex(row + added, ListView.Beginning)
        const retained = itemAtIndex(row + added)
        if (retained) contentY = retained.y + inset
        Qt.callLater(() => { transcript.loadingOlder = false })
    }
    header: Item {
        width: transcript.width
        height: transcript.controller.messages.hasOlder ? 48 : 0
        Button {
            id: olderMessagesButton
            anchors.centerIn: parent
            visible: transcript.controller.messages.hasOlder
            text: "加载更早的消息 · 还有 " + (transcript.controller.messages.totalCount - transcript.controller.messages.count) + " 条"
            flat: true
            onClicked: transcript.loadPreviousPage(true)
            contentItem: Text { text: olderMessagesButton.text; color: Design.Theme.secondary; font.pixelSize: 12 }
            background: Rectangle { radius: 8; color: olderMessagesButton.hovered ? Design.Theme.hover : "transparent" }
        }
    }
    onMovementStarted: followingEnd = false
    onMovementEnded: {
        if (!selectionLayer.origin && !transcriptScrollBar.pressed && atYEnd) followingEnd = true
        if (!transcriptScrollBar.pressed && !transcriptWheel.animating && contentY - originY < 120 && transcript.controller.messages.hasOlder) loadPreviousPage()
    }
    ScrollBar.vertical: ScrollBar {
        id: transcriptScrollBar
        width: 6
        policy: ScrollBar.AsNeeded
        // cancelFlick 会同步触发 movementEnded，必须最后再关闭自动贴底。
        onPressedChanged: if (pressed) { transcriptWheel.stop(); transcript.followingEnd = false }
    }
    function revealLatest() {
        followingEnd = true
        Qt.callLater(() => transcript.positionViewAtEnd())
    }
    onCountChanged: { if (followingEnd) Qt.callLater(() => transcript.positionViewAtEnd()) }
    onContentHeightChanged: {
        // 富文本完成布局后再贴齐底部，避免误显示“回到最新消息”。
        if (followingEnd) latestLayout.restart()
    }
    Timer {
        id: latestLayout
        interval: 0
        onTriggered: if (transcript.followingEnd) { transcript.forceLayout(); transcript.positionViewAtEnd() }
    }
    // 不同消息类型分别复用，避免工具记录把已创建的长回复控件反复替换掉。
    delegate: DelegateChooser {
        role: "messageKind"
        DelegateChoice { roleValue: 1; delegate: MessageRow {} }
        DelegateChoice { roleValue: 2; delegate: MessageRow {} }
        DelegateChoice { roleValue: 3; delegate: MessageRow {} }
        DelegateChoice { roleValue: 4; delegate: MessageRow {} }
    }
    component MessageRow: Item {
        id: messageRow
        required property int index
        required property string messageId
        required property int messageKind
        required property string content
        required property string messageTitle
        required property int messageStatus
        required property bool expanded
        required property var contentBlocks
        required property var changedFiles
        required property var activity
        required property var activityGroup
        required property var attachments
        width: ListView.view.width
        height: messageView.implicitHeight
        property bool pooled: false
        Component.onCompleted: Design.PerformanceTrace.contentPhase(messageRow, 5, 0)
        ListView.onPooled: { pooled = true; Design.PerformanceTrace.contentPhase(messageRow, 5, 1) }
        ListView.onReused: { pooled = false; Design.PerformanceTrace.contentPhase(messageRow, 5, 2) }
        readonly property bool disclosureAnimating: messageView.disclosureAnimating
        Design.MessageDelegate {
            id: messageView
            pooled: messageRow.pooled
            width: Math.min(messageRow.width - 48, 760)
            anchors.horizontalCenter: parent.horizontalCenter
            controller: transcript.controller
            outerWheel: transcriptWheel
            messageId: messageRow.messageId
            rowIndex: messageRow.index
            messageKind: messageRow.messageKind
            content: messageRow.content
            messageTitle: messageRow.messageTitle
            messageStatus: messageRow.messageStatus
            expanded: messageRow.expanded
            contentBlocks: messageRow.contentBlocks
            changedFiles: messageRow.changedFiles
            activityGroup: messageRow.activityGroup
            activity: messageRow.activity
            attachments: messageRow.attachments
            onFileRequested: (path) => transcript.fileRequested(path)
            onImageRequested: (image) => transcript.imageRequested(image)
        }
    }
}
