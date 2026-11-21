pragma ComponentBehavior: Bound
import QtQuick

// 注册的只是可见命中和选区投影；完整原文始终由会话模型提供。
QtObject {
    id: root
    property var manager: null
    property var registeredManager: null
    property Item target: null
    property Flickable scrollView: null
    property string messageId: ""
    property int blockIndex: 0
    property int textLength: 0
    property var positionFor: null
    property var rangeFor: null
    property var projectRange: null
    property bool ready: false
    function refresh() { if (ready && manager && target && projectRange) manager.project(root) }
    onManagerChanged: {
        if (!ready) return
        if (registeredManager) registeredManager.unregisterPart(root)
        registeredManager = manager
        if (registeredManager) registeredManager.registerPart(root)
    }
    onMessageIdChanged: Qt.callLater(refresh)
    onBlockIndexChanged: Qt.callLater(refresh)
    onTextLengthChanged: Qt.callLater(refresh)
    Component.onCompleted: {
        ready = true
        registeredManager = manager
        if (manager) manager.registerPart(root)
    }
    Component.onDestruction: if (registeredManager) registeredManager.unregisterPart(root)
}
