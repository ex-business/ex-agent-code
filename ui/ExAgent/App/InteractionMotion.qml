pragma Singleton
import QtQuick

// 新输入优先：结束旧呈现动画，连续操作结束后再恢复动效，不排队播放过时状态。
QtObject {
    id: motion
    property int activeCount: 0
    property bool suppressed: false
    readonly property bool allowed: !suppressed
    signal interrupted()
    property Timer quiet: Timer {
        interval: 250
        onTriggered: motion.suppressed = false
    }
    function interruptForInput() {
        if (activeCount === 0 && !suppressed) return
        suppressed = true
        interrupted()
        quiet.restart()
    }
}
