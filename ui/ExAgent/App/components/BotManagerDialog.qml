pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    objectName: "botManager"
    required property ChatController controller
    title: "本机 Bot"
    anchors.centerIn: Overlay.overlay
    modal: true
    sizeClass: WorkbenchDialog.Wide
    standardButtons: Dialog.Close
    property var entries: []
    property string cursor
    property string errorText
    function api(action, params) {
        const reply = controller.bots.operate(action, params || {})
        errorText = reply.ok ? "" : reply.error
        return reply.ok ? reply.result : null
    }
    function refresh() {
        const count = Math.max(100, entries.length)
        let items = [], after = ""
        do {
            const result = api("list", {limit: 100, after: after})
            if (!result) return
            items = items.concat(result.items); after = result.next || ""
        } while (after && items.length < count)
        entries = items; cursor = after
    }
    function begin() { refresh(); open() }
    function state(entry) {
        const result = api("set", {id: entry.id, revision: entry.revision, state: entry.state === 1 ? 2 : 1})
        if (result) refresh()
    }
    function edit(entry) {
        if (entry) { entry = api("get", {id: entry.id}); if (!entry) return }
        editor.botId = entry?.id || ""
        editor.revision = entry?.revision || 0
        editor.projectId = controller.currentProjectId
        editor.workspaceId = controller.currentWorkspaceId
        nameField.text = entry?.name || ""
        roleField.text = entry?.role || ""
        errorText = ""; editor.open()
    }
    WorkbenchDialog {
        id: editor
        title: botId ? "编辑 Bot 职责" : "创建本机 Bot"
        anchors.centerIn: Overlay.overlay; modal: true
        property string botId
        property int revision
        property string projectId
        property string workspaceId
        standardButtons: Dialog.Cancel
        contentItem: ScrollView {
            implicitHeight: Math.min(editor.maximumBodyHeight, form.implicitHeight)
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: form; width: editor.availableWidth; spacing: 10
                Label { text: Design.UiText.get("名称"); Layout.fillWidth: true }
                DialogInput { id: nameField; Layout.fillWidth: true; maximumLength: 64; placeholderText: Design.UiText.get("例如：代码审查助手") }
                Label { text: Design.UiText.get("职责"); Layout.fillWidth: true }
                DialogTextArea { id: roleField; Layout.fillWidth: true; placeholderText: Design.UiText.get("说明它负责什么、输出什么，以及需要遵守的边界") }
                Label {
                    text: Design.UiText.get("本机 Codex · 专属聊天。新建后默认暂停，打开聊天配置执行账号，再恢复 Bot。现有权限不会自动扩大。")
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary
                }
                Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.danger }
                DialogAction {
                    text: Design.UiText.get(editor.botId ? "保存职责" : "创建 Bot")
                    enabled: !!nameField.text.trim() && !!roleField.text.trim() && roleField.text.length <= 8192
                    primary: true
                    onClicked: {
                        const params = {name: nameField.text, role: roleField.text}
                        if (editor.botId) { params.id = editor.botId; params.revision = editor.revision }
                        else { params.project = editor.projectId; params.workspace = editor.workspaceId }
                        if (root.api(editor.botId ? "update" : "create", params)) { editor.close(); root.refresh() }
                    }
                }
            }
        }
    }
    WorkbenchConfirmDialog {
        id: newChat
        title: "开始新的 Bot 聊天"
        message: "旧聊天保留在项目历史中；新聊天默认暂停，配置执行账号后再恢复。"
        primaryText: "新建聊天"
        property var entry: ({})
        onPrimaryRequested: if (root.api("new-chat", {id: entry.id, revision: entry.revision})) { close(); root.refresh() }
        detail: root.errorText
        detailIsError: !!root.errorText
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: Design.UiText.get("独立职责与聊天，沿用现有执行权限"); color: Design.Theme.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
            DialogAction { text: Design.UiText.get("刷新"); onClicked: root.refresh() }
            DialogAction { text: Design.UiText.get("创建 Bot"); onClicked: root.edit(null) }
        }
        Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; color: Design.Theme.danger; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { visible: root.entries.length === 0; text: Design.UiText.get("还没有 Bot。创建后会在当前项目中新增专属聊天。") ; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(400, root.maximumBodyHeight - 90, Math.max(110, contentHeight))
            clip: true; spacing: 18; reuseItems: true; model: root.entries
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property var modelData
                width: ListView.view.width - 14
                Label { text: row.modelData.name + " · " + Design.UiText.get(["准备中", "可接收输入", "已暂停"][row.modelData.state]); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true }
                Label { text: row.modelData.rolePreview; textFormat: Text.PlainText; maximumLineCount: 3; elide: Text.ElideRight; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.secondary }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    DialogAction { visible: row.modelData.state === 0; text: Design.UiText.get("继续准备"); onClicked: if (root.api("prepare", {id: row.modelData.id})) root.refresh() }
                    DialogAction { visible: row.modelData.state !== 0; text: Design.UiText.get("打开聊天"); onClicked: if (root.api("open", {id: row.modelData.id})) root.close() }
                    DialogAction { visible: row.modelData.state !== 0; text: Design.UiText.get(row.modelData.state === 1 ? "暂停" : "恢复"); onClicked: root.state(row.modelData) }
                    DialogAction { text: Design.UiText.get("编辑职责"); onClicked: root.edit(row.modelData) }
                    DialogAction { visible: row.modelData.state !== 0; text: Design.UiText.get("新聊天"); onClicked: { newChat.entry = row.modelData; root.errorText = ""; newChat.open() } }
                }
            }
        }
        DialogAction {
            visible: !!root.cursor; text: Design.UiText.get("加载更多")
            onClicked: { const result = root.api("list", {limit: 100, after: root.cursor}); if (result) { root.entries = root.entries.concat(result.items); root.cursor = result.next || "" } }
        }
    }
}
