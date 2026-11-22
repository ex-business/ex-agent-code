pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Item {
    id: root
    required property var summary
    signal fileRequested(string path)
    implicitHeight: 36
    Button {
        id: chip
        transform: Design.PressFeedback { button: chip }
        anchors.centerIn: parent
        implicitHeight: 34
        implicitWidth: labels.implicitWidth + 26
        Accessible.name: "查看 " + (root.summary.fileCount || 0) + " 个变更文件"
        onClicked: changes.open()
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle { radius: 17; color: Design.Theme.background; border.color: Design.Theme.border }
        contentItem: RowLayout {
            id: labels
            spacing: 4
            Text { text: (root.summary.fileCount || 0) + " 个文件已更改"; color: Design.Theme.muted; font.pixelSize: 13 }
            Text { text: "+" + (root.summary.added || 0); color: "#00a43c"; font.pixelSize: 13 }
            Text { text: "−" + (root.summary.removed || 0); color: "#e94b54"; font.pixelSize: 13 }
        }
    }
    Popup {
        id: changes
        parent: Overlay.overlay
        width: Math.min(620, parent ? parent.width - 40 : 600)
        height: Math.min(420, files.implicitHeight + 24)
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        padding: 12
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Design.Theme.disclosureDuration } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Design.Theme.pressDuration } }
        modal: true
        background: Rectangle { radius: 14; color: Design.Theme.background; border.color: Design.Theme.border }
        contentItem: Design.ChangedFilesCard {
            id: files
            files: root.summary.files || []
            onFileRequested: (path) => { changes.close(); root.fileRequested(path) }
        }
    }
}
