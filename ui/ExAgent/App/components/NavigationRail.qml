pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

Rectangle {
    id: root
    signal sidebarRequested()
    signal activityRequested()
    signal jumpRequested()
    signal inboxRequested()
    signal browserRequested()
    property int unread: 0
    property int pendingCount: 0
    color: Design.Theme.dark ? "#242424" : "#f5f5f5"
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12; anchors.bottomMargin: 10
        spacing: 10
        Repeater {
            model: ["home", "search", "clock", "bell", "globe", "grid"]
            delegate: Design.IconButton {
                id: button
                required property string modelData
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 36; implicitHeight: 34
                iconName: modelData
                tooltip: modelData === "home" ? Design.UiText.get("显示或收起导航") : modelData === "clock" ? Design.UiText.get("工作区活动") : modelData === "search" ? Design.UiText.get("跳转工作树或会话") : modelData === "bell" ? Design.UiText.get("通知") : modelData === "globe" ? Design.UiText.get("工作区浏览器") : "导航展示 · " + modelData
                onClicked: {
                    if (modelData === "globe") root.browserRequested()
                    else if (modelData === "home") root.sidebarRequested()
                    else if (modelData === "clock") root.activityRequested()
                    else if (modelData === "search") root.jumpRequested()
                    else if (modelData === "bell") root.inboxRequested()
                }
                Rectangle {
                    readonly property int count: button.modelData === "clock" ? root.pendingCount : button.modelData === "bell" ? root.unread : 0
                    visible: count > 0
                    anchors.right: parent.right; anchors.top: parent.top
                    width: Math.max(16, countText.implicitWidth + 6); height: 16; radius: 8
                    color: Design.Theme.accent
                    Text { id: countText; anchors.centerIn: parent; text: parent.count; color: Design.Theme.onAccent; font.pixelSize: 10 }
                }
            }
        }
        Item { Layout.fillHeight: true }
        Design.IconButton { Layout.alignment: Qt.AlignHCenter; iconName: "sun"; tooltip: Design.UiText.get("切换主题"); onClicked: Design.Theme.toggle() }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 25; implicitHeight: 25; radius: 13; color: "#2785b5"
            Text { anchors.centerIn: parent; text: "JH"; color: "white"; font.pixelSize: 9 }
        }
    }
}
