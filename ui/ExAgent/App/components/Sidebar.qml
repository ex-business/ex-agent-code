pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Rectangle {
    id: root
    required property ChatController controller
    signal collapseRequested()
    property alias searchField: searchInput
    property var folded: ({})
    readonly property var resources: controller.resourceConversations
    readonly property var rows: {
        const all = controller.projects.rows.slice()
        if (resources.length) {
            all.push({kind: ProjectNavigator.ProjectRow, project: "@resources", workspace: "", title: "历史资源"})
            for (const item of resources)
                all.push({kind: ProjectNavigator.SessionRow, project: "@resources", workspace: "", resource: item.id, title: item.title})
        }
        const query = searchInput.text.trim().toLocaleLowerCase()
        const matches = new Set()
        if (query) for (const item of all) {
            if (item.title.toLocaleLowerCase().includes(query)) {
                matches.add(item.project)
                if (item.workspace) matches.add(item.workspace)
            }
        }
        return all.filter(item => {
            if (query) return item.title.toLocaleLowerCase().includes(query)
                || (item.kind === ProjectNavigator.ProjectRow && matches.has(item.project))
                || (item.kind === ProjectNavigator.WorkspaceRow && matches.has(item.workspace))
            if (item.kind !== ProjectNavigator.ProjectRow && folded[item.project]) return false
            return item.kind !== ProjectNavigator.SessionRow || !item.workspace || !folded[item.workspace]
        })
    }
    function toggle(key: string) {
        const next = Object.assign({}, folded)
        next[key] = !next[key]
        folded = next
    }
    function showSearch() { searchInput.visible = true; searchInput.forceActiveFocus() }
    color: Design.Theme.sidebar
    FolderDialog {
        id: folder
        property string project
        title: "添加本地工作区"
        onAccepted: root.controller.projects.bindWorkspace(project, selectedFolder)
    }
    Dialog {
        id: taskDialog
        property string provider
        property string project
        property string workspace
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            taskName.clear()
            standardButton(Dialog.Ok).enabled = false
            taskName.forceActiveFocus()
        }
        onAccepted: root.controller.projects.createTask(provider, project, workspace, taskName.text)
        contentItem: TextField {
            id: taskName
            placeholderText: "任务名称"
            maximumLength: 80
            onTextChanged: if (taskDialog.visible) taskDialog.standardButton(Dialog.Ok).enabled = text.trim().length > 0
            onAccepted: if (text.trim()) taskDialog.accept()
        }
    }
    Dialog {
        id: createDialog
        title: "新建项目"
        anchors.centerIn: Overlay.overlay
        modal: true; width: 310
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            standardButton(Dialog.Ok).text = "创建"
            standardButton(Dialog.Cancel).text = "取消"
            standardButton(Dialog.Ok).enabled = false
            projectName.clear()
            projectName.forceActiveFocus()
        }
        onAccepted: root.controller.projects.createProject(projectName.text)
        contentItem: TextField {
            id: projectName
            Accessible.name: "项目名称"; placeholderText: "项目名称"; maximumLength: 120
            onTextChanged: if (createDialog.visible) createDialog.standardButton(Dialog.Ok).enabled = text.trim().length > 0
            onAccepted: if (text.trim()) createDialog.accept()
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10; spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Text { text: "项目"; font.pixelSize: 17; font.weight: Font.DemiBold; color: Design.Theme.text; Layout.fillWidth: true }
            Design.IconButton { iconName: "plus"; tooltip: "新建项目"; enabled: root.controller.projects.writable; onClicked: createDialog.open() }
            Design.IconButton { iconName: "new"; tooltip: "新聊天"; enabled: root.controller.projects.writable; onClicked: root.controller.newSession() }
            Design.IconButton { iconName: "search"; tooltip: "搜索项目与聊天"; onClicked: root.showSearch() }
        }
        TextField {
            id: searchInput
            visible: false; Layout.fillWidth: true
            placeholderText: "搜索项目、工作区或聊天"
            Keys.onEscapePressed: { clear(); visible = false }
        }
        ListView {
            id: tree
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true; model: root.rows
            property bool pixelWheelEnabled: true
            property bool pixelWheelAnimated: Design.Theme.motionEnabled
            readonly property bool pixelWheelMoving: wheel.kineticRunning
            function pixelScrollBy(x: real, y: real): point { return Qt.point(0, wheel.consumePixels(y)) }
            signal pixelScrollStarted()
            Design.SmoothWheel { id: wheel; view: tree }
            ScrollBar.vertical: ScrollBar { width: 4 }
            delegate: Design.ProjectTreeRow {
                required property var modelData
                entry: modelData; width: tree.width; height: 31
                readonly property string key: entry.workspace || entry.project
                readonly property bool library: entry.project === "@resources"
                writable: !library && root.controller.projects.writable
                collapsed: !!root.folded[key]
                canMove: !root.controller.previewMode && entry.project === root.controller.currentProjectId
                    && entry.workspace !== root.controller.currentWorkspaceId
                selected: root.controller.previewMode ? !!entry.resource && entry.resource === root.controller.currentResourceId
                    : !!entry.session && entry.session === root.controller.currentSessionId
                onToggled: root.toggle(key)
                onOpened: {
                    if (entry.resource) root.controller.loadResourceConversation(entry.resource)
                    else if (library) root.toggle(key)
                    else if (entry.session) root.controller.selectSession(entry.session)
                    else {
                        if (collapsed) root.toggle(key)
                        root.controller.openGroup(entry.project, entry.workspace)
                    }
                }
                onNewChat: { if (collapsed) root.toggle(key); root.controller.newGroupSession(entry.project, entry.workspace) }
                providers: root.controller.projects.taskProviders
                onTaskRequested: (provider, title) => {
                    taskDialog.provider = provider
                    taskDialog.title = title
                    taskDialog.project = entry.project
                    taskDialog.workspace = entry.workspace
                    taskDialog.open()
                }
                onBindFolder: { if (collapsed) root.toggle(key); folder.project = entry.project; folder.open() }
                onMoveHere: { if (collapsed) root.toggle(key); root.controller.moveToGroup(entry.project, entry.workspace) }
            }
        }
    }
}
