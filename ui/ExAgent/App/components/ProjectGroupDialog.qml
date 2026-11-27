pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ExAgent.App as Design

WorkbenchDialog {
    id: panel
    required property var chat
    property string scanId: ""
    property bool scanning: false
    property var repositories: []
    property var chosen: ({})
    property string error: ""
    title: "导入多仓库项目组"
    modal: true; anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Wide
    function stopScan() {
        if (scanning && scanId) chat.projectGroups.operate("cancel-scan", {id: scanId})
        scanning = false
    }
    onClosed: stopScan()
    onOpened: { error = ""; path.forceActiveFocus() }
    FolderDialog { id: picker; onAccepted: { path.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, "")); panel.repositories = []; panel.chosen = ({}) } }
    Timer {
        interval: 120; repeat: true; running: panel.scanning && panel.visible
        onTriggered: {
            const r = panel.chat.projectGroups.operate("scan-result", {id: panel.scanId})
            if (!r.ok) { panel.scanning = false; panel.error = r.error; return }
            if (r.result.state === 0) return
            panel.scanning = false
            panel.error = r.result.error || (r.result.state !== 1 ? Design.UiText.get("扫描已取消") : "")
            panel.repositories = r.result.repos || []
            const choices = {}; for (const repo of panel.repositories) choices[repo.path] = true
            panel.chosen = choices
            if (!name.text) name.text = r.result.root.split("/").pop()
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; text: Design.UiText.get("选择包含多个 Git 仓库的父目录。仅登记直接子仓库，不复制文件或执行初始化脚本。"); wrapMode: Text.Wrap; color: Design.Theme.secondary }
        RowLayout {
            Layout.fillWidth: true
            DialogInput { id: path; Layout.fillWidth: true; placeholderText: Design.UiText.get("父目录绝对路径"); enabled: !panel.scanning; onTextEdited: { panel.repositories = []; panel.chosen = ({}) } }
            DialogAction { text: Design.UiText.get("选择目录"); enabled: !panel.scanning; onClicked: picker.open() }
            DialogAction {
                text: Design.UiText.get(panel.scanning ? "取消扫描" : "扫描仓库")
                onClicked: {
                    if (panel.scanning) { panel.stopScan(); return }
                    panel.repositories = []; panel.chosen = ({}); panel.error = ""
                    const r = panel.chat.projectGroups.operate("scan", {path: path.text.trim()})
                    if (!r.ok) panel.error = r.error
                    else { panel.scanId = r.result.id; panel.scanning = true }
                }
            }
        }
        DialogInput { id: name; Layout.fillWidth: true; maximumLength: 120; placeholderText: Design.UiText.get("项目组名称") }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: 200
            clip: true; model: panel.repositories; spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckBox {
                required property var modelData
                width: ListView.view.width
                text: modelData.name + " · " + modelData.path
                checked: !!panel.chosen[modelData.path]
                onToggled: { const v = Object.assign({}, panel.chosen); v[modelData.path] = checked; panel.chosen = v }
            }
        }
        Label { Layout.fillWidth: true; visible: !!panel.error; text: panel.error; wrapMode: Text.Wrap; color: Design.Theme.text; textFormat: Text.PlainText }
    }
    footer: WorkbenchDialogActions {
        primaryText: "导入项目组"
        primaryEnabled: !panel.scanning && !!name.text.trim() && Object.keys(panel.chosen).some(p => panel.chosen[p])
        onCancelled: panel.reject()
        onPrimaryRequested: {
            const r = panel.chat.projectGroups.operate("import", {scan: panel.scanId, name: name.text, paths: Object.keys(panel.chosen).filter(p => panel.chosen[p])})
            if (!r.ok) panel.error = r.error
            else { panel.close(); folders.present(r.result.id) }
        }
    }
    FolderWorkspaceDialog { id: folders; chat: panel.chat }
}
