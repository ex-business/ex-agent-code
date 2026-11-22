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
        layoutAnchor = null
        waitingOlder = false; previousScrollTime = 0; upwardVelocity = 0
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
        if (followingEnd || layoutAnchor || loadingOlder || moving) return
        const row = indexAt(width / 2, contentY + 2)
        const item = row >= 0 ? itemAtIndex(row) : null
        const message = item as MessageRow
        if (message) layoutAnchor = {id: message.messageId, offset: contentY - message.y}
    }
    function restoreLayoutAnchor() {
        const anchor = layoutAnchor; layoutAnchor = null
        if (!anchor || followingEnd || loadingOlder || moving) return
        forceLayout()
        const row = selectionDocument.orderOf(anchor.id)
        const item = row >= 0 ? itemAtIndex(row) : null
        if (item) contentY = Math.max(originY - topMargin, Math.min(Math.max(originY - topMargin, originY + contentHeight - height + bottomMargin), item.y + anchor.offset))
    }
    objectName: "messageList"
    property bool pixelWheelEnabled: true
    signal pixelScrollStarted()
    anchors.fill: parent
    visible: transcript.controller.messages.count > 0
    clip: true
    pixelAligned: true
    reuseItems: true
    cacheBuffer: Math.min(height * 1.5, 1600)
    spacing: 5
    topMargin: 24
    bottomMargin: 15
    boundsBehavior: Flickable.DragAndOvershootBounds
    acceptedButtons: Qt.NoButton
    model: transcript.controller.messages
    property bool followingEnd: true
    property bool loadingOlder: false
    property real previousScrollY: 0
    property real previousScrollTime: 0
    property real upwardVelocity: 0
    property bool waitingOlder: false
    onContentYChanged: {
        const now = Date.now()
        const delta = previousScrollY - contentY
        if (previousScrollTime > 0 && now > previousScrollTime) upwardVelocity = Math.max(0, delta * 1000 / (now - previousScrollTime))
        previousScrollY = contentY; previousScrollTime = now
        const runway = Math.min(height * 4, Math.max(height, upwardVelocity * 0.18))
        if (delta > 0 && contentY - originY < runway && transcript.controller.messages.hasOlder) transcript.controller.messages.prefetchOlder()
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
        onScrollingStarted: { transcript.layoutAnchor = null; transcript.followingEnd = false }
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
        required property var attachments
        width: ListView.view.width
        height: messageView.implicitHeight
        Design.MessageDelegate {
            id: messageView
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
            activity: messageRow.activity
            attachments: messageRow.attachments
            onFileRequested: (path) => transcript.fileRequested(path)
            onImageRequested: (image) => transcript.imageRequested(image)
        }
    }
}
