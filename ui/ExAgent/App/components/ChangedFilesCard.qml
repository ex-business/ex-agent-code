pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Rectangle {
    id: root
    required property var files
    property string messageId: ""
    property var selectionManager: null
    property var outerWheel: null
    property bool expanded: true
    property bool showAll: false
    property bool disclosureActive: false
    function animateDisclosure() { if (outerWheel) outerWheel.view.beginContentDisclosure(); disclosureActive = true; disclosureReset.restart() }
    Timer { id: disclosureReset; interval: 16; onTriggered: { if (fileDisclosure.running) restart(); else root.disclosureActive = false } }
    // 虚拟列表复用卡片时清除上一条消息的展开与滚动状态。
    onFilesChanged: {
        disclosureActive = false; disclosureReset.stop()
        expanded = true
        showAll = false
        fileList.contentY = 0
    }
    signal fileRequested(string path)
    implicitHeight: column.implicitHeight + 20
    radius: 13
    color: Design.Theme.background
    border.color: Design.Theme.border
    ColumnLayout {
        id: column
        x: 12; y: 10; width: parent.width - 24
        spacing: 0
        ListView {
            id: fileList
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? Math.min(280, (root.showAll ? root.files.length : Math.min(3, root.files.length)) * 35) : 0
            Design.DisclosureHeight on Layout.preferredHeight { id: fileDisclosure; allowed: root.disclosureActive }
            clip: true
            reuseItems: true
            acceptedButtons: Qt.NoButton
            model: root.expanded || root.disclosureActive ? root.showAll ? root.files : root.files.slice(0, 3) : []
            ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded }
            delegate: Button {
                id: file
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                required property var modelData
                required property int index
                width: fileList.width
                height: 35
                implicitHeight: 35
                Accessible.name: "查看文件 " + modelData.path
                onClicked: root.fileRequested(modelData.path)
                background: Rectangle { radius: 5; color: file.hovered ? Design.Theme.hover : "transparent" }
                contentItem: RowLayout {
                    spacing: 6
                    Design.SelectableText { text: file.modelData.path; textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap; messageId: root.messageId; blockIndex: file.index; selectionManager: root.selectionManager; Layout.fillWidth: true; clip: true; color: Design.Theme.secondary; font.pixelSize: 13; onTapped: root.fileRequested(file.modelData.path) }
                    Text { visible: typeof file.modelData.added === "number"; text: "+" + (file.modelData.added || 0); color: "#00a43c"; font.pixelSize: 12 }
                    Text { visible: typeof file.modelData.removed === "number"; text: "−" + (file.modelData.removed || 0); color: "#e94b54"; font.pixelSize: 12 }
                }
            }
        }
        Button {
            visible: root.expanded && !root.showAll && root.files.length > 3
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            text: "再显示 " + (root.files.length - 3) + " 个文件"
            flat: true
            onClicked: { root.animateDisclosure(); root.showAll = true }
        }
        Button {
            id: toggle
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            text: root.expanded ? "收起文件  ⌃" : "展开 " + root.files.length + " 个文件  ⌄"
            Accessible.name: text
            onClicked: { root.animateDisclosure(); root.expanded = !root.expanded }
            background: Item {}
            contentItem: Text { text: toggle.text; color: Design.Theme.text; font.pixelSize: 13 }
        }
    }
}
