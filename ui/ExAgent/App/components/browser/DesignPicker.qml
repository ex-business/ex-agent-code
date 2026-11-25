pragma ComponentBehavior: Bound
import QtQuick
import QtWebEngine
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: picker
    required property var view
    required property string sessionId
    signal picked(string session, var context, var image)
    signal rejected(string reason)
    signal cancelled()
    property var highlight: []
    property int revision: 0
    property int hoverSerial: 0
    property bool capturing: false

    function reset() {
        revision++
        hoverSerial++
        hoverTimer.stop()
        capturing = false
        highlight = []
    }
    function sample(x: real, y: real, take: bool) {
        if (!visible || !view || capturing || width <= 0 || height <= 0) return
        const nx = x / width, ny = y / height
        const code = probe.script(nx, ny, take)
        if (!code) return
        const activeView = view, epoch = revision, ticket = ++hoverSerial
        if (take) capturing = true
        activeView.runJavaScript(code, WebEngineScript.ApplicationWorld, function(data) {
            if (epoch !== picker.revision || activeView !== picker.view || !picker.visible || ticket !== picker.hoverSerial) return
            if (!data || !data.rect) {
                picker.highlight = []
                picker.capturing = false
                if (take) picker.rejected(Design.UiText.get(data && data.restricted ? "请选择表单或嵌入内容之外的元素" : "未能定位页面元素"))
                return
            }
            picker.highlight = data.rect
            if (!take) return
            const session = picker.sessionId
            const pageUrl = activeView.url.toString()
            const scale = Math.min(1, 1280 / Math.max(activeView.width,activeView.height))
            // 只截网页图层，选框及 IDE 的其他面板不会进入会话附件。
            const ok = activeView.grabToImage(function(result) {
                if (epoch !== picker.revision || session !== picker.sessionId || !picker.visible) return
                // 在异步抓帧后复核元素及位置，换页、滚动或布局变化时要求重新点选。
                activeView.runJavaScript(code, WebEngineScript.ApplicationWorld, function(now) {
                    if (epoch !== picker.revision || session !== picker.sessionId || !picker.visible) return
                    picker.capturing = false
                    if (pageUrl !== activeView.url.toString() || !now || now.selector !== data.selector ||
                            JSON.stringify(now.rect) !== JSON.stringify(data.rect) || now.markup !== data.markup) {
                        picker.rejected(Design.UiText.get("页面已变化，请重新选择元素"))
                        return
                    }
                    data.pageUrl = pageUrl
                    picker.picked(session, data, result.image)
                })
            }, Qt.size(Math.max(1,Math.round(activeView.width*scale)),Math.max(1,Math.round(activeView.height*scale))))
            if (!ok) { picker.capturing = false; picker.rejected(Design.UiText.get("未能截取网页画面")) }
        })
    }
    onVisibleChanged: { reset(); if (visible) forceActiveFocus() }
    onViewChanged: reset()
    onSessionIdChanged: reset()
    Keys.onEscapePressed: cancelled()
    DesignProbe { id: probe }
    Connections {
        target: picker.view
        function onLoadingChanged(info) { if (info.status === WebEngineView.LoadStartedStatus) picker.cancelled() }
    }
    Timer {
        id: hoverTimer; interval: 40
        onTriggered: picker.sample(pointer.mouseX,pointer.mouseY,false)
    }
    Rectangle {
        visible: picker.highlight.length === 4
        x: (picker.highlight[0] || 0)*picker.width; y: (picker.highlight[1] || 0)*picker.height
        width: (picker.highlight[2] || 0)*picker.width; height: (picker.highlight[3] || 0)*picker.height
        color: Qt.rgba(0.17,0.49,0.91,0.12); border.color: "#458beb"; border.width: 1
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.AllButtons
        onPositionChanged: if (!hoverTimer.running && !picker.capturing) hoverTimer.start()
        onExited: { if (!picker.capturing) { picker.hoverSerial++; picker.highlight = []; hoverTimer.stop() } }
        onClicked: event => { if (event.button === Qt.LeftButton) picker.sample(event.x,event.y,true); else picker.cancelled() }
        onWheel: event => { event.accepted = false }
    }
}
