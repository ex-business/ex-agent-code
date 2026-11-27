pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    objectName: "sshWorkspaces"
    required property ChatController controller
    title: "SSH 工作区"
    anchors.centerIn: Overlay.overlay
    modal: true; sizeClass: WorkbenchDialog.Wide; standardButtons: Dialog.Close
    property var hosts: []
    property var workspaces: []
    property var runs: []
    property var pending: []
    property string errorText
    property string notice
    function api(action, params) {
        const value = controller.ssh.operate(action, params || {})
        if (!value.ok) errorText = value.error
        return value.ok ? value.result : null
    }
    function all(action) {
        let rows = [], after = ""
        do {
            const result = api(action, {limit: 100, after: after})
            if (!result) return rows
            rows = rows.concat(result.items); after = result.next || ""
        } while (after)
        return rows
    }
    function refresh() { hosts = all("hosts"); workspaces = all("workspaces"); runs = all("runs") }
    function begin() { refresh(); open() }
    function launch(action, params, callback) {
        if (action !== "read") errorText = ""
        const result = api(action, params)
        if (result) {
            pending = pending.concat([{id: result.request, done: callback}])
            notice = Design.UiText.get("正在通过 SSH 处理…")
        }
        return result
    }
    Timer {
        interval: 300; repeat: true; running: root.pending.length > 0
        onTriggered: {
            const batch = root.pending
            root.pending = []
            const remaining = []
            for (const job of batch) {
                const value = root.api("request", {id: job.id})
                if (value?.state === 0) remaining.push(job)
                else {
                    if (value?.state === 3) root.errorText = value.error
                    job.done(value?.state === 2 ? value.result : null)
                }
            }
            root.pending = root.pending.concat(remaining)
            if (!root.pending.length) root.notice = ""
        }
    }
    WorkbenchDialog {
        id: addHost
        title: "添加 SSH 主机"; anchors.centerIn: Overlay.overlay; modal: true; standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { text: Design.UiText.get("使用已配置的 OpenSSH 别名与 known_hosts，不保存密码或私钥。"); wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.secondary }
            DialogInput { id: hostName; Layout.fillWidth: true; maximumLength: 80; placeholderText: Design.UiText.get("显示名称") }
            DialogInput { id: hostAlias; Layout.fillWidth: true; maximumLength: 128; placeholderText: Design.UiText.get("SSH 配置别名") }
            DialogInput { id: configFile; Layout.fillWidth: true; maximumLength: 4096; placeholderText: Design.UiText.get("配置文件绝对路径（留空使用系统默认）") }
            DialogInput { id: storage; Layout.fillWidth: true; maximumLength: 4096; placeholderText: Design.UiText.get("远程运行存储绝对路径（可留空）") }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.danger }
            DialogAction {
                text: Design.UiText.get("保存主机"); primary: true; enabled: !!hostName.text.trim() && !!hostAlias.text.trim()
                onClicked: if (root.api("add", {name: hostName.text, alias: hostAlias.text, config: configFile.text, storage: storage.text})) { addHost.close(); root.refresh() }
            }
        }
    }
    WorkbenchDialog {
        id: bind
        title: "关联远程仓库"; anchors.centerIn: Overlay.overlay; modal: true; standardButtons: Dialog.Cancel
        property string hostId
        property string projectId
        contentItem: ColumnLayout {
            Label { text: Design.UiText.get("填写此主机已有的 Git 仓库绝对路径。文件保留在远端。"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
            DialogInput { id: remotePath; Layout.fillWidth: true; maximumLength: 4096; placeholderText: Design.UiText.get("远程仓库绝对路径") }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.danger }
            DialogAction { text: Design.UiText.get("关联并检查"); primary: true; onClicked: if (root.launch("bind", {host: bind.hostId, project: bind.projectId, path: remotePath.text}, result => root.refresh())) bind.close() }
        }
    }
    WorkbenchDialog {
        id: worktree
        title: "创建远程任务工作树"; anchors.centerIn: Overlay.overlay; modal: true; standardButtons: Dialog.Cancel
        property string source
        contentItem: ColumnLayout {
            DialogInput { id: branch; Layout.fillWidth: true; maximumLength: 200; placeholderText: Design.UiText.get("新分支，例如 feature/task") }
            DialogInput { id: start; Layout.fillWidth: true; maximumLength: 200; text: "HEAD"; placeholderText: Design.UiText.get("起点分支或提交") }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.danger }
            DialogAction { text: Design.UiText.get("后台创建"); primary: true; onClicked: if (root.launch("worktree", {workspace: worktree.source, branch: branch.text, start: start.text}, result => root.refresh())) worktree.close() }
        }
    }
    WorkbenchDialog {
        id: command
        title: "远程运行"; anchors.centerIn: Overlay.overlay; modal: true; sizeClass: WorkbenchDialog.Wide; standardButtons: Dialog.Close
        property string workspaceId
        property string runId
        property double offset: 0
        property double logSize: 0
        property int generation: 0
        property string output
        property bool reading: false
        property bool launching: false
        property int state: 4
        function read() {
            if (!runId || reading || launching) return
            reading = true
            const token = generation
            const result = root.launch("read", {run: runId, offset: offset}, value => {
                if (token !== generation) return
                reading = false
                if (!value) { state = 4; return }
                output = (output + value.text).slice(-524288); offset = value.offset; logSize = value.size; state = value.state
                if (value.detail) root.errorText = value.detail
                if (value.totalBytes > value.storedBytes) root.notice = Design.UiText.get("远程日志已达到 16 MiB 保存上限")
                root.refresh()
            })
            if (!result) reading = false
        }
        Timer { interval: 1200; repeat: true; running: command.visible && !command.launching && !!command.runId && (command.state < 2 || (command.state <= 3 && command.offset < command.logSize)); onTriggered: command.read() }
        contentItem: ColumnLayout {
            DialogTextArea { id: script; Layout.fillWidth: true; placeholderText: Design.UiText.get("在此远程工作区执行的 Shell 命令") }
            RowLayout {
                DialogAction {
                    text: Design.UiText.get("执行"); primary: true; enabled: !!script.text.trim() && (!command.runId || command.state === 2 || command.state === 3) && !command.reading && !command.launching
                    onClicked: {
                        command.output = ""; command.offset = 0; command.logSize = 0
                        const token = command.generation
                        command.launching = true
                        const result = root.launch("run", {workspace: command.workspaceId, script: script.text}, value => {
                            if (token !== command.generation) return
                            command.launching = false
                            command.state = value ? value.state : 4; command.read()
                        })
                        if (result) { command.runId = result.run; command.state = 0 }
                        else command.launching = false
                    }
                }
                DialogAction { text: Design.UiText.get("重连读取"); enabled: !!command.runId && !command.reading; onClicked: command.read() }
                Label { text: Design.UiText.get(["已登记", "运行中", "已完成", "执行失败", "待核对"][command.state]); color: Design.Theme.secondary }
            }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.danger }
            Design.CodeBlock { Layout.fillWidth: true; code: command.output; language: "text"; controller: root.controller; maximumCodeHeight: Math.max(80, Math.min(240, command.maximumBodyHeight - 190)) }
        }
    }
    contentItem: ScrollView {
        implicitHeight: Math.min(root.maximumBodyHeight, body.implicitHeight, 500)
        contentWidth: availableWidth; clip: true
        ColumnLayout {
            id: body; width: root.availableWidth; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label { text: Design.UiText.get("连接可断开，远程运行记录继续保留"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
                DialogAction { text: Design.UiText.get("刷新"); onClicked: { root.errorText = ""; root.refresh() } }
                DialogAction { text: Design.UiText.get("添加主机"); onClicked: { root.errorText = ""; addHost.open() } }
            }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.danger }
            Label { text: root.notice; visible: !!text; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
            Repeater {
                model: root.hosts
                delegate: ColumnLayout {
                    id: hostRow; required property var modelData; Layout.fillWidth: true
                    Label { text: hostRow.modelData.name + " · " + hostRow.modelData.alias + " · " + Design.UiText.get(["未检查", "最近检查通过", "连接失败"][hostRow.modelData.state] || "未检查"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        DialogAction { text: Design.UiText.get("检查连接"); onClicked: root.launch("probe", {host: hostRow.modelData.id}, value => root.refresh()) }
                        DialogAction { text: Design.UiText.get("关联仓库"); onClicked: { bind.hostId = hostRow.modelData.id; bind.projectId = root.controller.currentProjectId; root.errorText = ""; bind.open() } }
                        DialogAction { text: Design.UiText.get("断开传输"); onClicked: root.api("disconnect", {host: hostRow.modelData.id}) }
                    }
                }
            }
            Repeater {
                model: root.workspaces
                delegate: ColumnLayout {
                    id: workspaceRow; required property var modelData; Layout.fillWidth: true
                    Label { text: workspaceRow.modelData.path + " · " + Design.UiText.get(["待检查", "已就绪", "需重连"][workspaceRow.modelData.state] || "待核对"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        DialogAction { text: Design.UiText.get("重新检查"); onClicked: root.launch("check", {workspace: workspaceRow.modelData.id}, value => root.refresh()) }
                        DialogAction { text: Design.UiText.get("任务工作树"); enabled: workspaceRow.modelData.state === 1; onClicked: { worktree.source = workspaceRow.modelData.id; worktree.open() } }
                        DialogAction { text: Design.UiText.get("运行命令"); enabled: workspaceRow.modelData.state === 1; onClicked: { command.generation++; command.reading = false; command.launching = false; command.logSize = 0; command.workspaceId = workspaceRow.modelData.id; command.runId = ""; command.offset = 0; command.output = ""; command.state = 4; script.text = ""; command.open() } }
                    }
                }
            }
            Repeater {
                model: root.runs
                delegate: DialogAction {
                    required property var modelData
                    Layout.fillWidth: true
                    text: Design.UiText.get("查看运行") + " · " + modelData.id.slice(0, 8) + " · " + modelData.scriptPreview.slice(0, 40)
                    onClicked: { command.generation++; command.reading = false; command.launching = false; command.logSize = 0; command.workspaceId = modelData.workspace; command.runId = modelData.id; command.offset = 0; command.output = ""; command.state = modelData.state; script.text = ""; command.open(); command.read() }
                }
            }
        }
    }
}
