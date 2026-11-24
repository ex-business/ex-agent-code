pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

Rectangle {
    id: root
    signal sidebarRequested()
    color: Design.Theme.dark ? "#242424" : "#f5f5f5"
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12; anchors.bottomMargin: 10
        spacing: 10
        Repeater {
            model: ["home", "folder", "clock", "chat", "dots", "grid"]
            delegate: Design.IconButton {
                required property string modelData
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 36; implicitHeight: 34
                iconName: modelData
                tooltip: modelData === "home" ? Design.UiText.get("显示或收起导航") : "导航展示 · " + modelData
                onClicked: if (modelData === "home") root.sidebarRequested()
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
