pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 原图像素与视口适配分别计算，小图片默认不放大；缩放及平移只属于查看器。
Item {
    id: canvas
    required property Design.MediaSession session
    property bool interactive: false
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property int reloadRevision: 0
    readonly property real naturalWidth: Math.max(1, Number(session.frame.width) || current.implicitWidth)
    readonly property real naturalHeight: Math.max(1, Number(session.frame.height) || current.implicitHeight)
    readonly property real fitScale: Math.min(1, width / naturalWidth, height / naturalHeight)
    readonly property real actualScale: fitScale * zoom
    readonly property var current: session.frameIndex % 2 === 0 ? bufferA : bufferB
    readonly property bool ready: current.status === Image.Ready
    readonly property bool failed: current.status === Image.Error || (session.frames.length > 0 && String(session.sourceForFrame(session.frameIndex)).length === 0)
    function toggleScale() {
        if (Math.abs(actualScale - 1) < 0.01) fit()
        else original()
        console.debug("图片缩放切换，blockId：", session.block.blockId || "附件", "比例：", actualScale)
    }
    function fit() { zoom = 1; panX = 0; panY = 0 }
    function original() { zoom = 1 / Math.max(0.001, fitScale); panX = 0; panY = 0 }
    function clampPan() {
        const dx = Math.max(0, (naturalWidth * actualScale - width) / 2)
        const dy = Math.max(0, (naturalHeight * actualScale - height) / 2)
        panX = Math.max(-dx, Math.min(dx, panX)); panY = Math.max(-dy, Math.min(dy, panY))
    }
    function magnify(factor: real) { zoom = Math.max(0.25, Math.min(16 / Math.max(0.001, fitScale), zoom * factor)); clampPan() }
    function sourceFor(parity: int): url {
        const retry = reloadRevision
        let index = session.frameIndex
        if (index % 2 !== parity) index = index + 1 < session.frames.length ? index + 1 : session.loopMode === session.repeatMode ? 0 : index
        return session.sourceForFrame(index)
    }
    function retry() { ++session.resourceRevision; bufferA.source = ""; bufferB.source = ""; ++reloadRevision; bufferA.source = Qt.binding(() => sourceFor(0)); bufferB.source = Qt.binding(() => sourceFor(1)) }
    onWidthChanged: clampPan()
    onHeightChanged: clampPan()
    Connections { target: canvas.session; function onFrameIndexChanged() { canvas.fit() } }
    Binding { target: canvas.session; property: "frameReady"; value: canvas.ready }
    Binding { target: canvas.session; property: "frameError"; value: canvas.failed }
    clip: true
    Rectangle { anchors.fill: parent; radius: 8; color: Design.Theme.dark ? "#141414" : "#f4f4f5" }
    Image {
        id: bufferA
        visible: canvas.session.frameIndex % 2 === 0
        x: (canvas.width - bufferA.width) / 2 + canvas.panX; y: (canvas.height - bufferA.height) / 2 + canvas.panY
        width: canvas.naturalWidth * canvas.actualScale; height: canvas.naturalHeight * canvas.actualScale
        source: canvas.sourceFor(0)
        asynchronous: true; cache: false; sourceSize: canvas.interactive ? Qt.size(4096, 4096) : Qt.size(960, 960)
        fillMode: Image.PreserveAspectFit
        Accessible.name: "当前图片 " + (canvas.session.frameIndex + 1)
    }
    Image {
        id: bufferB
        visible: canvas.session.frameIndex % 2 === 1
        x: (canvas.width - bufferB.width) / 2 + canvas.panX; y: (canvas.height - bufferB.height) / 2 + canvas.panY
        width: canvas.naturalWidth * canvas.actualScale; height: canvas.naturalHeight * canvas.actualScale
        source: canvas.sourceFor(1)
        asynchronous: true; cache: false; sourceSize: canvas.interactive ? Qt.size(4096, 4096) : Qt.size(960, 960)
        fillMode: Image.PreserveAspectFit
        Accessible.name: "当前图片 " + (canvas.session.frameIndex + 1)
    }
    Column {
        anchors.centerIn: parent; spacing: 8
        visible: !canvas.ready
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: canvas.failed ? "图片暂时无法读取" : canvas.session.frames.length ? "正在加载图片…" : "等待图片资源"; color: Design.Theme.secondary; font.pixelSize: 12 }
        Design.MediaButton { anchors.horizontalCenter: parent.horizontalCenter; visible: canvas.failed; text: "重新加载"; onClicked: canvas.retry() }
    }
    MouseArea {
        anchors.fill: parent
        enabled: canvas.interactive && canvas.ready
        property real lastX: 0
        property real lastY: 0
        property real pressX: 0
        property real pressY: 0
        property real lastClickTime: 0
        property real lastClickX: 0
        property real lastClickY: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : canvas.zoom > 1 ? Qt.OpenHandCursor : Qt.ArrowCursor
        onPressed: (mouse) => { lastX = pressX = mouse.x; lastY = pressY = mouse.y }
        onPositionChanged: (mouse) => { if (pressed) { canvas.panX += mouse.x - lastX; canvas.panY += mouse.y - lastY; canvas.clampPan(); lastX = mouse.x; lastY = mouse.y } }
        // 原生双击和普通点击计数共用入口，兼容后台窗口没有携带平台双击标记的情况。
        onDoubleClicked: { lastClickTime = 0; canvas.toggleScale() }
        onClicked: (mouse) => {
            if (Math.abs(mouse.x - pressX) > 6 || Math.abs(mouse.y - pressY) > 6) { lastClickTime = 0; return }
            const now = Date.now()
            if (now - lastClickTime < 400 && Math.abs(mouse.x - lastClickX) < 8 && Math.abs(mouse.y - lastClickY) < 8) { lastClickTime = 0; canvas.toggleScale() }
            else { lastClickTime = now; lastClickX = mouse.x; lastClickY = mouse.y }
        }
        onWheel: (wheel) => { canvas.magnify(Math.pow(1.0015, wheel.angleDelta.y || wheel.pixelDelta.y)); wheel.accepted = true }
    }

}
