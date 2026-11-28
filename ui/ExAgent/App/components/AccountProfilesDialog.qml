pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    required property ChatController controller
    readonly property AgentController agent: controller.agent
    readonly property var profiles: agent.accounts
    property string selectedId: ""
    property string feedback: ""
    readonly property var selected: {
        const rows = profiles.rows
        for (let i = 0; i < rows.length; ++i) if (rows[i].id === selectedId) return rows[i]
        return ({})
    }
    function label(id: string): string {
        const rows = profiles.rows
        for (let i=0; i<rows.length; ++i) if(rows[i].id===id) return rows[i].name
        return id
    }
    function limitText(limits): string {
        if(!limits) return ""
        const parts=[]
        for(const key of ["primary","secondary"]) {
            const w=limits[key]
            if(!w || w.usedPercent === undefined) continue
            parts.push(Design.UiText.get(key === "primary" ? "主窗口" : "次窗口") + " " + w.usedPercent + "%" + (w.resetsAt ? " · " + new Date(w.resetsAt*1000).toISOString() : ""))
        }
        return parts.join("\n")
    }
    function status(value: int): string { return Design.UiText.get(["未核验", "可用", "已达限额", "已停用", "需要登录"][value] || "未知") }
    title: "账号与用量"
    modal: true
    anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Wide
    onOpened: { accountSearch.clear(); selectedId = agent.selectedProfile; nextModel.text = agent.model; automatic.checked = agent.autoAccountSwitch; feedback = "" }
    Connections { target: root.profiles; function onFailure(reason: string) { if(root.visible) root.feedback=reason } }
    Connections { target: root.agent; function onFailure(reason: string) { if(root.visible) root.feedback=reason } }
    footer: WorkbenchDialogActions {
        primaryText: "从下一轮使用"
        primaryEnabled: root.profiles.writable && !!root.selected.id && root.selected.enabled === 1 && (root.selected.kind !== 3 || nextModel.text.trim().length > 0)
        onPrimaryRequested: {
            if(root.agent.useProfile(root.selectedId,nextModel.text,automatic.checked))root.close()
            else if(!root.feedback)root.feedback=Design.UiText.get("当前不能切换账号。")
        }
        onCancelled: root.close()
    }
    contentItem: ScrollView {
        id: body
        clip: true
        contentWidth: availableWidth
        implicitHeight: Math.min(form.implicitHeight, root.maximumBodyHeight)
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: form; width: body.availableWidth; spacing: 12
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                text: root.agent.busy ? Design.UiText.get("当前运行仍使用：") + root.label(root.agent.runningProfile) + "\n" + Design.UiText.get("切换仅从下一轮生效，不会中断或重放当前任务。") : Design.UiText.get("选择下一轮使用的账号与模型。")
            }
            Label { visible: !!root.feedback || !!root.profiles.error; text: root.feedback || root.profiles.error; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.warningBorder }
            DialogInput { id: accountSearch; Layout.fillWidth: true; placeholderText: Design.UiText.get("搜索账号"); Accessible.name: Design.UiText.get("搜索账号") }
            ListView {
                id: accountList
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(count*78,312)
                clip: true; spacing: 12; reuseItems: true; cacheBuffer: 156
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: root.visible ? root.profiles.rows.filter(function(item) { return (item.name || "").toLowerCase().indexOf(accountSearch.text.trim().toLowerCase()) >= 0 }) : []
                delegate: AbstractButton {
                    id: row
                    required property var modelData
                    width: accountList.width; implicitHeight: 66
                    onClicked: { root.selectedId=modelData.id; nextModel.text=modelData.model || root.agent.model; root.feedback="" }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    background: Rectangle { radius: 8; color: root.selectedId === row.modelData.id ? root.palette.button : row.hovered ? root.palette.base : "transparent"; border.color: root.selectedId === row.modelData.id ? root.palette.highlight : root.palette.mid }
                    contentItem: Column {
                        leftPadding: 12; rightPadding: 12; topPadding: 8; spacing: 5
                        Text { text: row.modelData.name + " · " + root.status(row.modelData.status); color: root.palette.text; font.pixelSize: 13; font.weight: Font.Medium; textFormat: Text.PlainText }
                        Text { width: parent.width-24; elide: Text.ElideRight; color: Design.Theme.tokens.panelPlaceholderText; font.pixelSize: 11; textFormat: Text.PlainText
                            text: (row.modelData.plan || Design.UiText.get("套餐未知")) + " · " + Design.UiText.get("已上报 Token：") + row.modelData.tokens + " · " + Design.UiText.get("已上报运行：") + row.modelData.knownRuns }
                    }
                    Accessible.name: modelData.name + "，" + root.status(modelData.status)
                }
            }
            RowLayout {
                DialogAction { text: Design.UiText.get("添加账号…"); enabled: root.profiles.writable; onClicked: create.open() }
                DialogAction { text: root.selected.enabled === 1 ? Design.UiText.get("停用") : Design.UiText.get("启用"); enabled: !!root.selected.id && root.profiles.writable; onClicked: root.profiles.setEnabled(root.selectedId,root.selected.enabled !== 1) }
                DialogAction { text: Design.UiText.get("下次手动核验"); enabled: !!root.selected.id && root.profiles.writable; onClicked: root.profiles.resetAvailability(root.selectedId) }
                DialogAction { visible: !!root.selected.home; text: Design.UiText.get("登录所选账号"); enabled: root.controller.projects.writable && root.profiles.writable && root.selected.enabled === 1 && !root.agent.authenticating; onClicked: root.agent.loginProfile(root.selectedId) }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Design.Theme.tokens.panelPlaceholderText
                text: (root.selected.owner || "") + (root.selected.scope ? "\n"+Design.UiText.get("计费范围：")+root.selected.scope : "") + (root.selected.limits ? "\n"+root.limitText(root.selected.limits) : "") }
            Label { text: Design.UiText.get("下一轮模型") }
            DialogInput { id: nextModel; Layout.fillWidth: true; maximumLength: 128; placeholderText: root.selected.kind === 3 ? Design.UiText.get("OpenAI API 模型 ID") : Design.UiText.get("Codex 默认模型") }
            CheckBox {
                id: automatic; text: Design.UiText.get("新一轮遇限额时自动选择已验证的候选账号"); Layout.fillWidth: true
                indicator: Rectangle { implicitWidth: 16; implicitHeight: 16; radius: 4; y: (automatic.height-height)/2; color: automatic.checked ? root.palette.windowText : root.palette.base; border.color: root.palette.mid; Text { anchors.centerIn: parent; text: "✓"; visible: automatic.checked; color: root.palette.window } }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.tokens.panelPlaceholderText; font.pixelSize: 12
                text: Design.UiText.get("只选择同引擎、模型相容且已验证的账号；不重试已发送的消息。Token 为已收到的运行快照，不代表账单金额；未知套餐和缺失用量不会估算。") }
            Label { visible: root.profiles.coverage === 2; Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("统计容量已满：这里只显示已保留的上报记录，完整快照仍保存在对应聊天。") }
            Label { visible: !root.profiles.usageReady && !root.profiles.error; text: Design.UiText.get("正在加载用量记录…") }
        }
    }
    WorkbenchDialog {
        id: create
        title: "添加账号"; modal: true; anchors.centerIn: Overlay.overlay; sizeClass: WorkbenchDialog.Compact
        onOpened: { name.clear(); credential.clear(); model.clear(); scope.clear(); root.feedback="" }
        footer: WorkbenchDialogActions {
            primaryText: "添加"; primaryEnabled: !!name.text.trim()
            onPrimaryRequested: {
                const id=root.profiles.create(kind.currentIndex===0?1:3,name.text,credential.text,model.text,scope.text)
                if(id){root.selectedId=id;nextModel.text=model.text;create.close()}
            }
            onCancelled: create.close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            DialogSelect { id: kind; Layout.fillWidth: true; translateOptions: true; model: ["独立 Codex 登录", "OpenAI API 凭据引用"] }
            DialogInput { id: name; Layout.fillWidth: true; placeholderText: Design.UiText.get("账号名称"); maximumLength: 80 }
            DialogInput { id: credential; visible: kind.currentIndex===1; Layout.fillWidth: true; placeholderText: Design.UiText.get("私有 env 文件绝对路径；留空使用启动环境") }
            DialogInput { id: model; Layout.fillWidth: true; placeholderText: Design.UiText.get("默认模型（可在使用时修改）"); maximumLength: 128 }
            DialogInput { id: scope; Layout.fillWidth: true; placeholderText: Design.UiText.get("计费范围标识（可选，同范围不互相切换）"); maximumLength: 200 }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Design.Theme.tokens.panelPlaceholderText; text: kind.currentIndex===0 ? Design.UiText.get("使用独立私有登录目录，创建后点击登录所选账号。") : Design.UiText.get("这里只保存文件路径，不保存密钥；新账号先手动发送一次完成核验。") }
            Label { visible: !!root.feedback; text: root.feedback; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }
}
