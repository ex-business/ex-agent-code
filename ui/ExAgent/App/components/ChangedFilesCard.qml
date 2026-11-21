pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Rectangle {
    id: root
    property bool expanded: true
    implicitHeight: expanded ? 154 : 40
    radius: 13
    color: Design.Theme.background
    border.color: Design.Theme.border
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 0
        Repeater {
            model: root.expanded ? [
                { path: "ui/ExAgent/App/Main.qml", added: "+3", removed: "−2" },
                { path: "ui/ExAgent/App/components/CodeBlock.qml", added: "+2", removed: "−2" },
                { path: "聊天界面实现记录.md", added: "+15", removed: "−1" }
            ] : []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 35
                Text { text: parent.modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle; color: Design.Theme.secondary; font.pixelSize: 13 }
                Text { text: parent.modelData.added; color: "#00a43c"; font.pixelSize: 12 }
                Text { text: parent.modelData.removed; color: "#e94b54"; font.pixelSize: 12 }
            }
        }
        Button {
            id: toggle
            text: root.expanded ? "收起文件  ⌃" : "展开文件  ⌄"
            Accessible.name: text + "（展示样例）"
            onClicked: root.expanded = !root.expanded
            background: Item {}
            contentItem: Text { text: toggle.text; color: Design.Theme.text; font.pixelSize: 13 }
        }
    }
}
