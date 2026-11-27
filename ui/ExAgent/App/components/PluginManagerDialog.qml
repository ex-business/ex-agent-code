pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    objectName: "pluginManager"
    required property ChatController controller
    title: "插件管理"
    anchors.centerIn: Overlay.overlay
    modal: true
    sizeClass: WorkbenchDialog.Wide
    standardButtons: Dialog.Close
    property var entries: []
    property string cursor
    property string errorText
    function api(action, params) {
        const response = controller.plugins.operate(action, params || {})
        errorText = response.ok ? "" : response.error
        return response.ok ? response.result : null
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
    function installed(id) {
        let after = ""
        do {
            const result = api("list", {limit: 100, after: after})
            if (!result) return false
            if (result.items.some(e => e.id === id)) return true
            after = result.next || ""
        } while (after)
        return false
    }
    function begin() { refresh(); open() }
    function approve(entry) {
        const id = entry.id, hash = entry.hash, grants = entry.capabilities
        if (api("approve", {id: id, hash: hash, grants: grants})) api("enable", {id: id, hash: hash, enabled: 1})
    }
    Connections {
        target: root.controller.plugins
        function onChanged() {
            root.refresh()
            const item = root.entries.find(e => e.id === panelView.pluginId)
            if (panelView.visible && (!item || item.state !== 1 || item.hash !== panelView.versionHash)) panelView.close()
            const commandItem = root.entries.find(e => e.id === commandView.pluginId)
            if (commandView.visible && (!commandItem || commandItem.state !== 1 || commandItem.hash !== commandView.versionHash)) commandView.close()
        }
    }
    FolderDialog {
        id: folder
        title: Design.UiText.get("选择本地插件目录")
        onAccepted: {
            review.directory = selectedFolder.toString()
            const manifest = root.api("inspect", {directory: review.directory})
            if (manifest) { review.upgrading = root.installed("local." + manifest.id); review.manifest = manifest; review.open() }
        }
    }
    WorkbenchDialog {
        id: review
        title: "检查插件"
        anchors.centerIn: Overlay.overlay; modal: true
        property string directory
        property var manifest: ({})
        property bool upgrading: false
        standardButtons: Dialog.Cancel
        contentItem: ScrollView {
            implicitHeight: Math.min(review.maximumBodyHeight, reviewBody.implicitHeight)
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: reviewBody
                width: review.availableWidth
            Label { text: (review.manifest.name || "") + " · " + (review.manifest.version || ""); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: review.manifest.description || ""; textFormat: Text.PlainText; visible: !!text; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: Design.UiText.get("声明的宿主能力"); color: Design.Theme.secondary }
            Label { text: (review.manifest.capabilities || []).join("\n") || Design.UiText.get("静态数据贡献"); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            DialogAction {
                text: Design.UiText.get(review.upgrading ? "保存新版本，稍后授权" : "安装，稍后授权")
                primary: true
                onClicked: if (root.api(review.upgrading ? "upgrade" : "install", {directory: review.directory})) review.close()
            }
            Label { visible: !!root.errorText; text: root.errorText; textFormat: Text.PlainText; color: Design.Theme.danger; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
        }
    }
    WorkbenchDialog {
        id: panelView
        title: "插件内容"
        anchors.centerIn: Overlay.overlay; modal: true; sizeClass: WorkbenchDialog.Wide
        standardButtons: Dialog.Close
        property string pluginId
        property string versionHash
        property string body
        property var panels: []
        contentItem: ColumnLayout {
            RowLayout {
            Layout.fillWidth: true
            DialogAction { text: Design.UiText.get("上一项"); enabled: panelSelector.currentIndex > 0; onClicked: panelSelector.decrementCurrentIndex() }
            DialogSelect {
                id: panelSelector
                Layout.fillWidth: true; model: panelView.panels.map(p => p.id)
                onCurrentIndexChanged: {
                    if (currentIndex < 0) return
                    const value = root.api("panel", {id: panelView.pluginId, panel: panelView.panels[currentIndex].id})
                    if (value) panelView.body = value.text
                }
            }
            DialogAction { text: Design.UiText.get("下一项"); enabled: panelSelector.currentIndex + 1 < panelSelector.count; onClicked: panelSelector.incrementCurrentIndex() }
            }
            ScrollView {
            Layout.fillWidth: true
            implicitHeight: Math.min(360, panelView.maximumBodyHeight); contentWidth: availableWidth; clip: true
            Design.SelectableText { width: panelView.availableWidth; text: panelView.body; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: Design.Theme.text }
            }
        }
    }
    WorkbenchDialog {
        id: commandView
        title: "插件命令"
        anchors.centerIn: Overlay.overlay; modal: true; sizeClass: WorkbenchDialog.Wide
        standardButtons: Dialog.Close
        property string pluginId
        property string versionHash
        property var commands: []
        property string resultText
        contentItem: ColumnLayout {
            DialogSelect {
                id: command; Layout.fillWidth: true
                model: commandView.commands.map(c => c.id)
                onCurrentIndexChanged: parameters.text = JSON.stringify(commandView.commands[currentIndex]?.params || {})
            }
            Label { text: commandView.commands[command.currentIndex]?.title || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            DialogInput { id: parameters; Layout.fillWidth: true; maximumLength: 65536; placeholderText: Design.UiText.get("命令参数（JSON 对象）") }
            DialogAction {
                text: Design.UiText.get("执行命令"); enabled: command.currentIndex >= 0; primary: true
                onClicked: {
                    try {
                        const result = root.api("invoke", {id: commandView.pluginId, hash: commandView.versionHash, command: command.currentText, params: JSON.parse(parameters.text)})
                        if (result) commandView.resultText = JSON.stringify(result, null, 2)
                    } catch (e) { root.errorText = Design.UiText.get("命令参数必须是 JSON 对象") }
                }
            }
            Label { visible: !!root.errorText; text: root.errorText; textFormat: Text.PlainText; color: Design.Theme.danger; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Design.CodeBlock {
                Layout.fillWidth: true; visible: !!commandView.resultText
                code: commandView.resultText; language: "json"; controller: root.controller
                maximumCodeHeight: Math.max(80, Math.min(260, commandView.maximumBodyHeight - 130))
            }
        }
    }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            Label { text: Design.UiText.get("本地声明式插件"); color: Design.Theme.secondary; Layout.fillWidth: true }
            DialogAction { text: Design.UiText.get("安装或升级…"); onClicked: folder.open() }
        }
        Label { visible: !!root.errorText; text: root.errorText; textFormat: Text.PlainText; color: Design.Theme.danger; Layout.fillWidth: true; wrapMode: Text.Wrap }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(400, root.maximumBodyHeight - 90, Math.max(120, contentHeight))
            clip: true; spacing: 12; reuseItems: true; model: root.entries
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property var modelData
                width: ListView.view.width - 14
                Label { text: row.modelData.name + " · " + row.modelData.version; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label { text: row.modelData.id + " · " + Design.UiText.get(["待授权", "已启用", "已禁用"][row.modelData.state]); textFormat: Text.PlainText; color: Design.Theme.secondary }
                Label { text: row.modelData.capabilities.join(" · ") || Design.UiText.get("静态数据贡献"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    DialogAction { visible: row.modelData.authorization === 0; text: Design.UiText.get("同意能力并启用"); onClicked: root.approve(row.modelData) }
                    DialogAction { visible: row.modelData.state === 2 && row.modelData.authorization === 1; text: Design.UiText.get("启用"); onClicked: root.api("enable", {id: row.modelData.id, hash: row.modelData.hash, enabled: 1}) }
                    DialogAction { visible: row.modelData.state === 1; text: Design.UiText.get("禁用"); onClicked: root.api("enable", {id: row.modelData.id, enabled: 0}) }
                    DialogAction {
                        visible: row.modelData.state === 1; text: Design.UiText.get("查看贡献")
                        onClicked: {
                            const result = root.api("panels", {id: row.modelData.id})
                            if (result?.items.length) {
                                const panel = root.api("panel", {id: row.modelData.id, panel: result.items[0].id})
                                if (panel) { panelView.pluginId = row.modelData.id; panelView.versionHash = row.modelData.hash; panelView.body = panel.text; panelView.panels = result.items; panelView.open() }
                            } else root.errorText = Design.UiText.get("此插件没有静态面板")
                        }
                    }
                    DialogAction {
                        visible: row.modelData.state === 1; text: Design.UiText.get("命令")
                        onClicked: {
                            const result = root.api("get", {id: row.modelData.id})
                            if (result) {
                                commandView.pluginId = result.id; commandView.versionHash = result.hash; commandView.commands = result.manifest.commands
                                command.currentIndex = 0; parameters.text = JSON.stringify(commandView.commands[0]?.params || {}); commandView.resultText = ""; commandView.open()
                            }
                        }
                    }
                }
                RowLayout {
                    visible: row.modelData.versions.length > 1
                    DialogSelect { id: releases; Layout.fillWidth: true; model: row.modelData.versions }
                    DialogAction {
                        text: Design.UiText.get("切换保留版本")
                        enabled: releases.currentText !== row.modelData.version
                        onClicked: root.api("rollback", {id: row.modelData.id, version: releases.currentText})
                    }
                }
            }
        }
        DialogAction {
            visible: !!root.cursor; text: Design.UiText.get("加载更多")
            onClicked: { const result = root.api("list", {limit: 100, after: root.cursor}); if (result) { root.entries = root.entries.concat(result.items); root.cursor = result.next || "" } }
        }
    }
}
