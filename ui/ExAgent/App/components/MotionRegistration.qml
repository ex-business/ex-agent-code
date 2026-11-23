pragma ComponentBehavior: Bound
import QtQml
import ExAgent.App as Design

// 控件销毁或复用时归还计数，避免全局误认为旧动画仍在运行。
QtObject {
    property bool running: false
    property bool registered: false
    onRunningChanged: reconcile()
    Component.onCompleted: reconcile()
    Component.onDestruction: if (registered) Design.InteractionMotion.activeCount = Math.max(0, Design.InteractionMotion.activeCount - 1)
    function reconcile() {
        if (registered === running) return
        registered = running
        Design.InteractionMotion.activeCount = Math.max(0, Design.InteractionMotion.activeCount + (running ? 1 : -1))
    }
}
