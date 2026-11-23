pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Rectangle {
    id: root
    required property ChatController controller
    signal collapseRequested()
    property alias searchField: searchInput
    color: Design.Theme.sidebar
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 9; anchors.rightMargin: 13; anchors.topMargin: 10; anchors.bottomMargin: 10
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 8; Layout.bottomMargin: 10
            Text { text: "ChatGPT ⌄"; color: Design.Theme.text; font.pixelSize: 18; font.weight: Font.Medium; Layout.fillWidth: true }
            Design.IconButton { implicitWidth: 26; iconName: "bell"; tooltip: "通知样式展示，尚未连接" }
            Design.IconButton { iconName: "search"; tooltip: "搜索聊天"; onClicked: { searchInput.visible = !searchInput.visible; if (searchInput.visible) searchInput.forceActiveFocus() } }
        }
        Button {
            id: newChat
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            Accessible.name: "新聊天"
            Layout.fillWidth: true; implicitHeight: 33
            onClicked: root.controller.newSession()
            background: Rectangle { radius: 8; color: newChat.hovered ? Design.Theme.hover : "transparent" }
            contentItem: RowLayout {
                Design.Icon { name: "new"; implicitWidth: 15; implicitHeight: 15 }
                Text { text: "新聊天"; color: Design.Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 8; Layout.topMargin: 5; Layout.bottomMargin: 17
            Text { text: "💜 你的 Dot"; color: Design.Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            Rectangle {
                visible: Number(root.controller.navigationInfo.unreadCount || 0) > 0
                implicitWidth: 20; implicitHeight: 20; radius: 10; color: Design.Theme.dark ? "#484848" : "#d5d5d5"
                Text { anchors.centerIn: parent; text: root.controller.navigationInfo.unreadCount || 0; color: Design.Theme.text; font.pixelSize: 11 }
            }
        }
        TextField {
            id: searchInput
            visible: false
            Layout.fillWidth: true
            implicitHeight: 32
            placeholderText: "搜索聊天"; font.pixelSize: 12
            color: Design.Theme.text
            background: Rectangle { radius: 8; color: Design.Theme.surface }
        }
        ListView {
            id: projects
            property bool pixelWheelEnabled: true
            property bool pixelWheelAnimated: Design.Theme.motionEnabled
            readonly property bool pixelWheelMoving: projectsWheel.kineticRunning
            function pixelScrollBy(x: real, y: real): point { return Qt.point(0, projectsWheel.consumePixels(y)) }
            signal pixelScrollStarted()
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true; cacheBuffer: 100
            pixelAligned: true
            Design.SmoothWheel { id: projectsWheel; view: projects }
            model: root.controller.navigationEntries
            ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: entry
                required property var modelData
                readonly property bool selected: root.controller.previewMode && modelData.title === root.controller.sessionTitle
                width: projects.width
                height: searchInput.text.length === 0 || modelData.title.toLowerCase().includes(searchInput.text.toLowerCase()) ? (modelData.kind === 0 ? 42 : 31) : 0
                visible: height > 0
                Button {
                    id: projectButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: projectButton.enabled }
                    anchors.fill: parent
                    enabled: entry.modelData.kind !== 0
                    leftPadding: entry.modelData.kind === 3 || entry.modelData.kind === 4 ? 31 : 8
                    // 侧栏按参考图展示条目；只有当前对话有实际展示数据，不伪造其他会话内容。
                    onClicked: {
                        if (entry.modelData.resourceId) root.controller.loadResourceConversation(entry.modelData.resourceId)
                    }
                    Accessible.name: entry.modelData.title + (entry.selected ? "，当前聊天" : "，导航样例")
                    background: Rectangle { radius: 9; color: entry.selected && root.controller.previewMode ? Design.Theme.selected : projectButton.hovered ? Design.Theme.hover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 8
                        Design.Icon { visible: entry.modelData.kind === 2; name: "folder"; implicitWidth: 15; implicitHeight: 15 }
                        Text { text: entry.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; color: entry.modelData.kind === 0 || entry.modelData.kind === 4 ? Design.Theme.muted : Design.Theme.text; font.pixelSize: 14 }
                    }
                }
            }
            footer: Column {
                width: projects.width
                Repeater {
                    model: root.controller.sessions
                    delegate: Button {
                        id: localSession
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        required property string sessionTitle
                        required property string sessionId
                        width: projects.width; height: 31
                        text: sessionTitle
                        onClicked: root.controller.selectSession(sessionId)
                        background: Rectangle { radius: 8; color: localSession.hovered ? Design.Theme.hover : "transparent" }
                        contentItem: Text { text: localSession.text; color: Design.Theme.secondary; elide: Text.ElideRight; font.pixelSize: 13; leftPadding: 8 }
                    }
                }
            }
        }
    }
}
