pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 仅用户改变展开状态时启用；列表复用、异步内容增长和滚动布局不做高度补间。
Behavior {
    property bool allowed: true
    property bool ready: false
    enabled: ready && allowed && Design.Theme.motionEnabled
    Component.onCompleted: ready = true
    readonly property bool running: heightMotion.running
    NumberAnimation { id: heightMotion; duration: Design.Theme.disclosureDuration; easing.type: Easing.OutCubic }
}
