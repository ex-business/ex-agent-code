pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

ColumnLayout {
    id: controls
    required property Design.MediaSession session
    spacing: 3
    Slider {
        id: timeline
        Layout.fillWidth: true
        from: 0; to: Math.max(0, controls.session.frames.length - 1); stepSize: 1
        value: controls.session.frameIndex
        enabled: controls.session.frames.length > 1
        Accessible.name: "图片序列进度"
        onMoved: controls.session.seek(Math.round(value))
        background: Rectangle { x: timeline.leftPadding; y: timeline.topPadding + timeline.availableHeight / 2 - 2; width: timeline.availableWidth; height: 3; radius: 2; color: Design.Theme.border; Rectangle { width: timeline.visualPosition * parent.width; height: parent.height; radius: 2; color: Design.Theme.secondary } }
        handle: Rectangle { x: timeline.leftPadding + timeline.visualPosition * (timeline.availableWidth - width); y: timeline.topPadding + timeline.availableHeight / 2 - height / 2; width: 10; height: 10; radius: 5; color: Design.Theme.text }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        Design.MediaButton { text: "上一张"; enabled: controls.session.frameIndex > 0; onClicked: controls.session.seek(controls.session.frameIndex - 1) }
        Design.MediaButton { text: controls.session.playbackState === controls.session.playingState ? "暂停" : "播放"; enabled: controls.session.frames.length > 1; onClicked: controls.session.toggle() }
        Design.MediaButton { text: "下一张"; enabled: controls.session.frameIndex + 1 < controls.session.frames.length; onClicked: controls.session.seek(controls.session.frameIndex + 1) }
        Text { text: (controls.session.frames.length ? controls.session.frameIndex + 1 : 0) + " / " + controls.session.frames.length; color: Design.Theme.secondary; font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
        Design.MediaButton { text: controls.session.speed + "×"; Accessible.name: "切换播放速度"; onClicked: controls.session.speed = controls.session.speed === 0.5 ? 1 : controls.session.speed === 1 ? 2 : 0.5 }
        Design.MediaButton { text: controls.session.loopMode === controls.session.repeatMode ? "循环开" : "循环关"; onClicked: controls.session.loopMode = controls.session.loopMode === controls.session.repeatMode ? controls.session.onceMode : controls.session.repeatMode }
    }
}
