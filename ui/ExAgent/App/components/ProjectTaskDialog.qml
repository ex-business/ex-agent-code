pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: taskDialog
    title: "新建任务"
    required property ProjectNavigator navigator
    property var settings: ({})
    property string initialWorkspace
    readonly property var workspaces: navigator.rows.filter(r => r.kind === ProjectNavigator.WorkspaceRow && r.project === project)
    function reload() {
        ready = false
        fields = []
        request++
        settings = navigator.launchSettings(provider, project, workspace)
        taskLink.text = settings.task || ""
        host.currentIndex = Object.keys(settings.hosts || {}).indexOf(settings.host || "local")
        configMessage.text = settings.error || (host.currentIndex < 0 ? "默认主机尚未就绪，请选择可用主机。" : "")
        if (!settings.error) navigator.loadTaskFields(provider, project, workspace, request)
    }
    property int request: 0
    property var fields: []
    property bool ready: false
    property string provider
    property string project
    property string workspace
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    sizeClass: WorkbenchDialog.Normal
    Connections {
        target: taskDialog.navigator
        function onTaskFieldsReady(request: int, fields: var) {
            if (request !== taskDialog.request) return
            taskDialog.fields = fields
            taskDialog.ready = true
        }
    }
    onOpened: {
        source.currentIndex = Math.max(0, workspaces.findIndex(r => r.workspace === initialWorkspace))
        workspace = workspaces[source.currentIndex]?.workspace || ""
        reload()
        taskName.clear()
        taskName.forceActiveFocus()
    }
    onAccepted: {
        const options = {host: Object.keys(settings.hosts || {})[host.currentIndex], task: taskLink.text}
        for (let i = 0; i < editors.count; ++i) {
            const editor = editors.itemAt(i) as TaskOptionField
            options[editor.modelData.key] = editor.value
        }
        taskDialog.navigator.createTask(provider, project, workspace, taskName.text, options)
    }
    component TaskOptionField: ColumnLayout {
        id: field
        required property var modelData
        readonly property var value: modelData.values ? modelData.values[choice.currentIndex] : modelData.choices ? choice.editText : input.text
        Layout.fillWidth: true
        Label { text: field.modelData.label }
        DialogSelect {
            id: choice
            visible: !!field.modelData.choices
            Layout.fillWidth: true
            editable: !field.modelData.values
            model: field.modelData.key === "start" && taskDialog.settings.start
                    ? [taskDialog.settings.start, ...(field.modelData.choices || []).filter(r => r !== taskDialog.settings.start)]
                    : field.modelData.choices || []
            Accessible.name: field.modelData.label
        }
        DialogInput {
            id: input
            visible: !field.modelData.choices
            Layout.fillWidth: true
            Accessible.name: field.modelData.label
        }
    }
    contentItem: ScrollView {
        implicitHeight: Math.min(form.implicitHeight, taskDialog.maximumBodyHeight)
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
        id: form
        width: taskDialog.availableWidth

        Label { text: "来源工作区" }
        DialogSelect {
            id: source
            Layout.fillWidth: true
            model: taskDialog.workspaces
            textRole: "title"
            onActivated: { taskDialog.workspace = taskDialog.workspaces[currentIndex].workspace; taskDialog.reload() }
        }
        Label { text: "运行主机" }
        DialogSelect {
            id: host
            Layout.fillWidth: true
            model: Object.values(taskDialog.settings.hosts || {})
            Accessible.name: "运行主机"
        }
        DialogInput {
            id: taskLink
            Layout.fillWidth: true
            placeholderText: "关联任务编号或链接（可选）"
            maximumLength: 2048
        }
        DialogInput {
            id: taskName
            Layout.fillWidth: true
            placeholderText: "任务名称"
            maximumLength: 80
            onAccepted: if (taskDialog.ready && host.currentIndex >= 0 && text.trim()) taskDialog.accept()
        }
        Binding {
            target: taskDialog.standardButton(Dialog.Ok)
            property: "enabled"
            value: taskDialog.ready && host.currentIndex >= 0 && taskName.text.trim().length > 0
        }
        Label {
            id: configMessage
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: !!text
        }
        DialogAction {
            text: "新建项目 orca.yaml"
            visible: !!taskDialog.workspace && !taskDialog.settings.exists
            onClicked: {
                const error = taskDialog.navigator.createLaunchTemplate(taskDialog.project, taskDialog.workspace)
                if (error) configMessage.text = error
                else taskDialog.reload()
            }
        }
        Repeater {
            id: editors
            model: taskDialog.fields
            delegate: TaskOptionField {}
        }
        }
    }
}
