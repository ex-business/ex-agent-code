pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: panel
    required property ChatController controller
    title: "监督任务"
    anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Wide
    modal: true
    standardButtons: Dialog.Close
    WorkbenchDialog {
        id: resultDialog
        title: "结果摘要（最多 8192 字符）"
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Close
        sizeClass: WorkbenchDialog.Wide
        property string resultText
        contentItem: ScrollView {
            implicitHeight: Math.min(420, resultDialog.maximumBodyHeight)
            TextArea { text: resultDialog.resultText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; background: null }
        }
    }
    property string exportingTask
    FolderDialog {
        id: exchangeDirectory
        title: Design.UiText.get("选择协作交换目录")
        onAccepted: panel.controller.agent.exportSupervisedTask(panel.exportingTask, remoteHost.text.trim(), selectedFolder)
    }
    FileDialog {
        id: exchangeFile
        title: Design.UiText.get("导入任务或结果")
        nameFilters: ["EX Worker (*.ex-worker.json)"]
        onAccepted: panel.controller.agent.importSupervisedExchange(selectedFile, worker.currentValue || "")
    }
    property string owner
    property var workers: []
    property var tasks: []
    function begin() {
        owner = controller.currentSessionId
        workers = controller.agent.correspondents
        tasks = controller.agent.supervisedTasks
        worker.currentIndex = workers.length ? 0 : -1
        open()
    }
    Connections {
        target: panel.controller.agent
        function onSupervisionChanged() {
            if (!panel.visible) return
            if (panel.owner !== panel.controller.currentSessionId) panel.close()
            else panel.tasks = panel.controller.agent.supervisedTasks
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: Design.UiText.get("实例 ID"); color: Design.Theme.secondary }
            DialogInput { Layout.fillWidth: true; text: panel.controller.agent.workerHost; readOnly: true; selectByMouse: true }
            DialogAction { text: Design.UiText.get("导入任务或结果"); onClicked: exchangeFile.open() }
        }
        DialogInput {
            id: remoteHost; Layout.fillWidth: true; maximumLength: 36
            placeholderText: Design.UiText.get("接收实例 ID（跨实例派发时填写）")
        }
        DialogSelect {
            id: worker; Layout.fillWidth: true
            model: panel.workers; textRole: "title"; valueRole: "id"
            Accessible.name: Design.UiText.get("Worker 会话")
        }
        DialogInput {
            id: specification; Layout.fillWidth: true; maximumLength: 8192
            placeholderText: Design.UiText.get("交给 worker 的任务")
        }
        RowLayout {
            CheckBox { id: gate; text: Design.UiText.get("派发前等待决策") }
            Item { Layout.fillWidth: true }
            DialogAction {
                text: Design.UiText.get("创建任务"); primary: true
                enabled: worker.currentIndex >= 0 && specification.text.trim().length > 0
                onClicked: if (panel.controller.agent.createSupervisedTask(worker.currentValue, specification.text, gate.checked)) specification.clear()
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.max(120, panel.maximumBodyHeight - 160))
            clip: true; spacing: 12; model: panel.tasks
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property var modelData
                width: ListView.view.width - 14
                Label {
                    Layout.fillWidth: true; textFormat: Text.PlainText
                    text: row.modelData.title + " · " + Design.UiText.get(["等待派发","等待决策","执行中","已完成","失败","待核对"][row.modelData.state] || "待核对")
                    color: Design.Theme.secondary
                }
                Label { Layout.fillWidth: true; text: row.modelData.text; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                Label {
                    Layout.fillWidth: true; visible: !!row.modelData.result
                    text: row.modelData.result || ""; textFormat: Text.PlainText
                    wrapMode: Text.Wrap; maximumLineCount: 6; elide: Text.ElideRight
                }
                RowLayout {
                    DialogAction {
                        visible: !!row.modelData.result && !row.modelData.remoteHost
                        text: Design.UiText.get("打开 worker 会话")
                        onClicked: panel.controller.selectSession(row.modelData.worker)
                    }
                    DialogAction {
                        visible: !!row.modelData.result; text: Design.UiText.get("查看结果摘要")
                        onClicked: { resultDialog.resultText = row.modelData.result; resultDialog.open() }
                    }
                    DialogAction {
                        visible: row.modelData.state === 1; text: Design.UiText.get("允许派发")
                        onClicked: panel.controller.agent.resolveSupervisedTask(row.modelData.id, true)
                    }
                    DialogAction {
                        visible: row.modelData.state === 1; text: Design.UiText.get("拒绝")
                        onClicked: panel.controller.agent.resolveSupervisedTask(row.modelData.id, false)
                    }
                    DialogAction {
                        visible: [0,4,5].includes(row.modelData.state) && !row.modelData.remoteEnvelope
                        text: Design.UiText.get("导出到实例…")
                        enabled: remoteHost.text.trim().length > 0
                        onClicked: { panel.exportingTask = row.modelData.id; exchangeDirectory.open() }
                    }
                    DialogAction {
                        visible: [0,4,5].includes(row.modelData.state) && !row.modelData.remoteHost
                        text: Design.UiText.get(row.modelData.state === 0 ? "派发" : "重新派发")
                        onClicked: panel.controller.agent.dispatchSupervisedTask(row.modelData.id)
                    }
                }
            }
        }
    }
}
