pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    readonly property var frames: Design.BlockRendererRegistry.values((root.block.data || {}).frames, 500)
    property int frameIndex: 0
    property int playbackState: 1 // 1暂停、2播放、3播完。
    property real speed: 1
    property int loopMode: 1 // 1不循环、2循环。
    readonly property var frame: frames[Math.min(frameIndex, Math.max(0, frames.length - 1))] || ({})
    implicitHeight: frames.length ? (root.detached ? 570 : 360) : 100
    property string restoredId: ""
    function remember() {
        if (root.block.blockId && restoredId === root.block.blockId && frames.length) root.controller.blockViews.savePresentation(root.block.blockId, {frameIndex: frameIndex, speed: speed, loopMode: loopMode, playbackState: playbackState})
    }
    function restore() {
        if (!root.block.blockId || root.restoredId === root.block.blockId || !frames.length) return

        const saved = root.controller.blockViews.presentation(root.block.blockId || "")
        frameIndex = Math.min(Math.max(0, Number(saved.frameIndex) || 0), Math.max(0, frames.length - 1))
        speed = Number(saved.speed) || 1
        loopMode = Number(saved.loopMode) || 1
        playbackState = Number(saved.playbackState) || 1
        restoredId = root.block.blockId
    }
    onBlockChanged: Qt.callLater(restore)
    Component.onCompleted: Qt.callLater(restore)
    onFrameIndexChanged: remember()
    onPlaybackStateChanged: remember()
    onFramesChanged: { if (frameIndex >= frames.length) frameIndex = Math.max(0, frames.length - 1) }
    onSpeedChanged: remember()
    onLoopModeChanged: remember()
    readonly property var currentImage: root.frameIndex % 2 === 0 ? bufferA : bufferB
    function bufferSource(buffer: int): url {
        if (!root.controller || !root.frames.length) return ""
        let index = root.frameIndex
        if (index % 2 !== buffer) index = index + 1 < root.frames.length ? index + 1 : root.loopMode === 2 ? 0 : index
        return root.controller.mediaSource(root.block, (root.frames[index] || {}).resourceId || "")
    }
    // 双缓冲直接交换显示帧，最多保留两份1600px解码结果，不依赖全序列全局缓存。
    Image {
        id: bufferA
        visible: root.frameIndex % 2 === 0
        width: parent.width; height: parent.height - 78
        source: root.bufferSource(0)
        asynchronous: true; cache: false; sourceSize: Qt.size(1600, 1600); fillMode: Image.PreserveAspectFit
        Accessible.name: "图片序列当前帧 " + (root.frameIndex + 1)
    }
    Image {
        id: bufferB
        visible: root.frameIndex % 2 === 1
        width: parent.width; height: parent.height - 78
        source: root.bufferSource(1)
        asynchronous: true; cache: false; sourceSize: Qt.size(1600, 1600); fillMode: Image.PreserveAspectFit
        Accessible.name: "图片序列当前帧 " + (root.frameIndex + 1)
    }
    Text { visible: root.currentImage.status !== Image.Ready; anchors.centerIn: bufferA; text: root.currentImage.status === Image.Error ? "当前帧读取失败，请选择其他帧" : root.frames.length ? "正在加载图片…" : root.block.text || "等待图片序列"; wrapMode: Text.Wrap; color: Design.Theme.secondary }
    RowLayout {
        anchors.bottom: progress.top; anchors.bottomMargin: 4; width: parent.width
        Button { text: root.playbackState === 2 ? "暂停" : "播放"; enabled: root.frames.length > 0; onClicked: { if (root.playbackState === 3) root.frameIndex = 0; root.playbackState = root.playbackState === 2 ? 1 : 2 } }
        Button { text: "上一帧"; enabled: root.frameIndex > 0; onClicked: { root.playbackState = 1; --root.frameIndex } }
        Button { text: "下一帧"; enabled: root.frameIndex + 1 < root.frames.length; onClicked: { root.playbackState = 1; ++root.frameIndex } }
        Text { text: (root.frames.length ? root.frameIndex + 1 : 0) + " / " + root.frames.length; color: Design.Theme.secondary }
        Item { Layout.fillWidth: true }
        ComboBox { model: ["0.5×", "1×", "2×"]; currentIndex: root.speed === 0.5 ? 0 : root.speed === 2 ? 2 : 1; onActivated: root.speed = [0.5, 1, 2][currentIndex]; Accessible.name: "播放速度" }
        CheckBox { text: "循环"; checked: root.loopMode === 2; onToggled: root.loopMode = checked ? 2 : 1 }
    }
    Slider { id: progress; anchors.bottom: parent.bottom; width: parent.width; from: 0; to: Math.max(0, root.frames.length - 1); stepSize: 1; value: root.frameIndex; onMoved: { root.playbackState = 1; root.frameIndex = Math.round(value) } Accessible.name: "图片序列进度" }
    Timer {
        interval: Math.max(20, ((root.block.data || {}).durationMs || 100) / root.speed)
        repeat: true
        running: root.visible && root.Window.window && root.Window.window.visible && root.Window.window.visibility !== Window.Minimized && root.playbackState === 2 && root.frames.length > 0
        onTriggered: {
            if (root.currentImage.status === Image.Error) { root.playbackState = 1; return }
            if (root.currentImage.status !== Image.Ready) return
            if (root.frameIndex + 1 < root.frames.length) ++root.frameIndex
            else if (root.loopMode === 2) root.frameIndex = 0
            else root.playbackState = 3
        }
    }
}
