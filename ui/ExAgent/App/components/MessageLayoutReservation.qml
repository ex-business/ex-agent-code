pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 一条消息的临时加载状态不改变已测量高度；缓存仅由完整布局更新，不能写入占位高度。
QtObject {
    id: reservation
    property Design.ScrollRenderPolicy policy: null
    property string key: ""
    property real contentWidth: 0
    property real naturalHeight: 0
    property bool ready: false
    readonly property real cachedHeight: policy && key ? policy.heightFor(key, contentWidth) : 0
    readonly property real height: ready || cachedHeight <= 0 ? naturalHeight : cachedHeight
    function remember() {
        if (policy && key && ready && naturalHeight > 0 && contentWidth > 0)
            policy.remember(key, contentWidth, naturalHeight)
        // 诊断只记录缓存命中阶段，不记录消息身份或正文。
        Design.PerformanceTrace.contentPhase(reservation, 4, ready ? 4 : cachedHeight > 0 ? 1 : 2)
    }
    onReadyChanged: Qt.callLater(remember)
    onNaturalHeightChanged: Qt.callLater(remember)
    onContentWidthChanged: Qt.callLater(remember)
    onKeyChanged: Qt.callLater(remember)
}
