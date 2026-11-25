pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Rectangle {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    property string locatedPath: ""
    property int mode: 0
    property bool renderedMarkdown: true
    color: Design.Theme.background
    Connections {
        target: root.workspace
        function onFileActivated() { root.locatedPath=""; root.mode=root.workspace.activePath ? 1 : 0 }
        function onPreviewChanged() { if (root.mode === 1 && !root.workspace.activePath) root.mode=0 }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 9
            spacing: 5
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34; radius: 17
                color: Design.Theme.background; border.color: Design.Theme.border
                Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter; text: root.mode === 1 ? root.workspace.activePath.replace(/\//g, "  ›  ") : !root.workspace.rootPath ? "此会话暂无可用工作区" : root.workspace.rootPath.replace(/\//g, "  ›  "); elide: Text.ElideLeft; color: Design.Theme.secondary; font.pixelSize: 12 }
            }
            Design.IconButton { iconName: root.mode === 0 ? "file" : "folder"; tooltip: root.mode === 0 ? "查看文件预览" : "浏览工作区文件"; enabled: root.mode !== 0 || root.workspace.activePath.length > 0; onClicked: root.mode = root.mode === 0 ? 1 : 0 }
            Design.IconButton { iconName: "branch"; tooltip: Design.UiText.get("工作区变更"); iconColor: root.mode === 2 ? Design.Theme.accent : Design.Theme.secondary; enabled: !!root.workspace.rootPath; onClicked: root.mode = root.mode === 2 ? 0 : 2 }
            Design.IconButton { visible: root.mode===1 && root.workspace.viewer.sourceToggle===true; iconName: "code"; tooltip: Design.UiText.get("切换源码和预览"); onClicked: filePreview.togglePreview() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
        RowLayout {
            visible: root.mode === 0
            Layout.fillWidth: true; Layout.margins: 9
            TextField {
                id: filter
                Layout.fillWidth: true; implicitHeight: 28
                placeholderText: "筛选文件…"; font.pixelSize: 12; color: Design.Theme.text
                leftPadding: 28
                background: Rectangle { radius: 14; color: Design.Theme.background; border.color: Design.Theme.border; Design.Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "search"; width: 13; height: 13 } }
            }
            Design.IconButton { iconName: "refresh"; tooltip: "刷新文件列表"; onClicked: root.workspace.refresh() }
        }
        Text { visible: root.workspace.error.length > 0; Layout.fillWidth: true; Layout.margins: 12; text: Design.UiText.get(root.workspace.error); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Design.Theme.warning; font.pixelSize: 12 }
        ListView {
            id: files
            visible: root.mode === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true
            model: root.workspace.entries
            ScrollBar.vertical: ScrollBar { width: 5 }
            delegate: Button {
                id: entry
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                required property var modelData
                width: files.width
                height: filter.text.length === 0 || modelData.path.toLowerCase().includes(filter.text.toLowerCase()) ? 29 : 0
                visible: height > 0
                leftPadding: 10 + modelData.depth * 16
                Accessible.name: (modelData.directory ? "文件夹 " : "预览文件 ") + modelData.path
                onClicked: {
                    if (modelData.directory) root.workspace.open(root.workspace.rootPath + "/" + modelData.path)
                    else root.workspace.openWorkspaceFile(modelData.path)
                }
                background: Rectangle { radius: 5; color: entry.modelData.path === root.locatedPath ? Design.Theme.selected : entry.hovered ? Design.Theme.hover : "transparent" }
                contentItem: RowLayout {
                    spacing: 6
                    Design.Icon { name: entry.modelData.directory ? entry.modelData.expanded ? "down" : "right" : "file"; implicitWidth: 13; implicitHeight: 13 }
                    Text { text: entry.modelData.name; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 12; color: Design.Theme.text }
                    Design.IconButton {
                        visible: !entry.modelData.directory && (entry.hovered || entry.activeFocus || activeFocus || root.locatedPath === entry.modelData.path)
                        implicitWidth: 26; implicitHeight: 26; iconSize: 14
                        iconName: "branch"; tooltip: Design.UiText.get("查看文件差异")
                        onClicked: {
                            root.mode = 2
                            changes.showPath(entry.modelData.path)
                        }
                    }
                }
            }
        }
        Design.WorkspaceGitChanges {
            id: changes
            chat: root.controller
            writable: root.workspace.writableWorkspace
            visible: root.mode === 2
            folder: root.workspace.rootPath
            Layout.fillWidth: true; Layout.fillHeight: true
            onOpenRequested: path => root.workspace.openWorkspaceFile(path)
            onRevealRequested: path => {
                const row = root.workspace.revealFile(path)
                if (row < 0) return
                filter.clear()
                root.locatedPath = path
                root.mode = 0
                files.currentIndex = row
                files.forceLayout()
                files.positionViewAtIndex(row, ListView.Contain)
                files.forceActiveFocus()
            }
        }
        Design.FilePreview { id: filePreview; visible: root.mode === 1; Layout.fillWidth: true; Layout.fillHeight: true; workspace: root.workspace; controller: root.controller; renderedMarkdown: root.renderedMarkdown }
    }
}
