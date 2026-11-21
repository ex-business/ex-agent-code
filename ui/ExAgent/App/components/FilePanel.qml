pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Rectangle {
    id: root
    color: Design.Theme.background
    property bool folderExpanded: false
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: Design.Theme.sidebar
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 12
                Text { text: "▧  架构设计    ⌘1"; color: Design.Theme.muted; font.pixelSize: 11 }
                Text { text: "▧  功能规划    ⌘2"; color: Design.Theme.muted; font.pixelSize: 11 }
                Text { text: "▧  Main.qml"; color: Design.Theme.secondary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
        Rectangle {
            Layout.fillWidth: true; Layout.margins: 9
            implicitHeight: 34; radius: 17; color: Design.Theme.background; border.color: Design.Theme.border
            Text { anchors.fill: parent; anchors.leftMargin: 12; verticalAlignment: Text.AlignVCenter; text: "Users  ›  tong  ›  Documents  ›  GitHub  ›  ex-agent-code.app"; elide: Text.ElideLeft; color: Design.Theme.secondary; font.pixelSize: 12 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
        TextField {
            id: filter
            Layout.fillWidth: true; Layout.margins: 9
            implicitHeight: 28
            placeholderText: "筛选文件…"; font.pixelSize: 12; color: Design.Theme.text
            placeholderTextColor: Design.Theme.muted
            leftPadding: 28
            background: Rectangle {
                radius: 14; color: Design.Theme.background; border.color: Design.Theme.border
                Design.Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "search"; implicitWidth: 13; implicitHeight: 13 }
            }
        }
        Button {
            id: contents
            Accessible.name: root.folderExpanded ? "收起 Contents" : "展开 Contents"
            Layout.fillWidth: true; implicitHeight: 28
            visible: filter.text.length === 0 || "contents".includes(filter.text.toLowerCase())
            onClicked: root.folderExpanded = !root.folderExpanded
            background: Rectangle { color: contents.hovered ? Design.Theme.hover : "transparent" }
            contentItem: RowLayout {
                Design.Icon { name: root.folderExpanded ? "down" : "right"; implicitWidth: 12; implicitHeight: 12 }
                Text { text: "Contents"; font.pixelSize: 12; color: Design.Theme.text; Layout.fillWidth: true }
            }
        }
        Repeater {
            model: root.folderExpanded ? ["MacOS", "Resources", "Info.plist"] : []
            delegate: Text {
                required property string modelData
                visible: filter.text.length === 0 || modelData.toLowerCase().includes(filter.text.toLowerCase())
                Layout.leftMargin: 29; Layout.topMargin: 7
                text: modelData; color: Design.Theme.secondary; font.pixelSize: 12
            }
        }
        Item { Layout.fillHeight: true }
    }
}
