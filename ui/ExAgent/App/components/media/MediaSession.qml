pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: session
    required property var block
    required property ChatController controller
    property var attachment: ({})
    property bool interactive: false
    // 状态仍使用整数契约，通过实例常量访问，避免QML自引用类型解析的循环依赖。
    readonly property int pausedState: 1
    readonly property int playingState: 2
    readonly property int endedState: 3
    readonly property int onceMode: 1
    readonly property int repeatMode: 2
    readonly property bool sequence: Number(block.rendererKind) === 6
    readonly property var frames: attachment.url ? [{label: attachment.name, width: attachment.width, height: attachment.height}] : sequence ? Design.BlockRendererRegistry.values((block.data || {}).frames, 500) : ((block.data || {}).resourceId ? [block.data] : [])
    property int frameIndex: 0
    property int playbackState: session.pausedState
    property int loopMode: session.onceMode
    property real speed: 1
    property string restoredId: ""
    readonly property var frame: frames[frameIndex] || ({})
    function sourceForFrame(index: int): url {
        const revision = resourceRevision
        if (attachment.url) return attachment.url
        return controller.mediaSource(block, (frames[index] || {}).resourceId || "")
    }
    property int resourceRevision: 0
    property bool frameReady: false
    property bool frameError: false
    function restore() {
        if (!block.blockId || restoredId === block.blockId || !frames.length) return
        const saved = controller.blockViews.presentation(block.blockId)
        frameIndex = Math.min(Math.max(0, Number(saved.frameIndex) || 0), frames.length - 1)
        speed = Number(saved.speed) || 1
        loopMode = Number(saved.loopMode) || session.onceMode
        playbackState = Number(saved.playbackState) || session.pausedState
        restoredId = block.blockId
    }
    function remember() {
        if (restoredId === block.blockId && frames.length) controller.blockViews.savePresentation(block.blockId, {frameIndex: frameIndex, speed: speed, loopMode: loopMode, playbackState: playbackState})
    }
    function seek(index: int) { playbackState = session.pausedState; frameIndex = Math.max(0, Math.min(frames.length - 1, index)) }
    function toggle() {
        if (playbackState === session.endedState) frameIndex = 0
        playbackState = playbackState === session.playingState ? session.pausedState : session.playingState
    }
    onBlockChanged: Qt.callLater(restore)
    onFramesChanged: { if (frameIndex >= frames.length) frameIndex = Math.max(0, frames.length - 1); Qt.callLater(restore) }
    onFrameIndexChanged: remember()
    onPlaybackStateChanged: remember()
    onLoopModeChanged: remember()
    onSpeedChanged: remember()
    Component.onCompleted: Qt.callLater(restore)
    readonly property int viewRevision: controller ? controller.blockViews.revision : 0
    // 返回聊天时刷新静态预览，不让旧预览反向覆盖查看器保存的播放位置。
    onViewRevisionChanged: if (!interactive && controller) { ++resourceRevision; restoredId = ""; restore() }
    Timer {
        interval: Math.max(20, Number((session.block.data || {}).durationMs || 100) / session.speed)
        repeat: true
        running: session.interactive && session.visible && session.Window.window && session.Window.window.visible && session.Window.window.visibility !== Window.Minimized && session.playbackState === session.playingState && session.frames.length > 1
        onTriggered: {
            if (session.frameError) { session.playbackState = session.pausedState; return }
            if (!session.frameReady) return
            if (session.frameIndex + 1 < session.frames.length) ++session.frameIndex
            else if (session.loopMode === session.repeatMode) session.frameIndex = 0
            else session.playbackState = session.endedState
        }
    }
}
