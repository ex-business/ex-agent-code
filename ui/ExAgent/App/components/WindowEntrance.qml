pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 开窗动效只变换窗口内部内容，不改变聊天布局或原生窗口坐标。
Item {
    id: surface
    property bool ready: false
    property bool entered: false
    property double startedAt: 0
    function present() {
        if (!ready || entered) return
        entered = true
        if (Design.Theme.motionEnabled) entrance.start()
    }
    onReadyChanged: present()
    Component.onCompleted: present()
    ParallelAnimation {
        id: entrance
        onStarted: { surface.startedAt = Date.now(); console.debug("内容窗口入场动画开始") }
        onFinished: console.debug("内容窗口入场动画完成，耗时：", Date.now() - surface.startedAt)
        NumberAnimation { target: surface; property: "opacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
        NumberAnimation { target: surface; property: "scale"; from: 0.985; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
}
