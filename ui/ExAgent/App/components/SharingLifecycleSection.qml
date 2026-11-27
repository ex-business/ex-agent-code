pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

// 邀请生命周期和审计是独立面板；移动接入模式不创建此组件或加载审计。
ColumnLayout {
    id: root
    required property var gateway
    required property string sessionId
    required property string sessionTitle
    required property var leaseInfo
    property var invites: []
    property var auditRows: []
    property int auditAfter: 0
    property int auditNext: 0
    property int auditEnd: 0
    property int auditState: 0
    signal failure(string reason)
    signal sharingClosed()
    spacing: 12
    function api(action, params) {
        const value = gateway.operate("sharing." + action, params)
        if (!value.ok) { failure(value.error); return null }
        return value.result
    }
    function refresh() {
        if (!sessionId) return
        const pending = api("invites", {session: sessionId})
        if (pending && JSON.stringify(invites) !== JSON.stringify(pending.items)) invites = pending.items
        const page = api("audit", {session: sessionId, after: auditAfter, limit: 20})
        if (page) {
            if (JSON.stringify(auditRows) !== JSON.stringify(page.items)) auditRows = page.items
            auditNext = page.next; auditEnd = page.end; auditState = page.auditState
        } else auditState = 1
    }
    function eventLabel(value) {
        const names = ["", "生成邀请", "撤销邀请", "设备配对", "撤销设备", "停止会话共享", "交接控制", "桌面收回", "申请控制", "协作者交还", "租约释放"]
        return Design.UiText.get(names[value] || "共享记录")
    }
    function phaseLabel(value) { return Design.UiText.get(value === 1 ? "操作开始" : value === 2 ? "已完成" : "未完成") }
    Component.onCompleted: refresh()
    Label { text: Design.UiText.get("待配对邀请"); font.bold: true; color: Design.Theme.text }
    Label { visible: root.invites.length === 0; text: Design.UiText.get("暂无有效邀请"); color: Design.Theme.secondary }
    Repeater {
        model: root.invites
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Label {
                text: modelData.id.slice(0, 8) + " · " + Design.UiText.get(modelData.scope === 1 ? "只读" : "允许发送") + " · " + new Date(modelData.expires * 1000).toLocaleTimeString()
                textFormat: Text.PlainText; color: Design.Theme.secondary
                Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            DialogAction { text: Design.UiText.get("撤销邀请"); onClicked: { if (root.api("cancel-invite", {session: root.sessionId, id: modelData.id})) root.sharingClosed(); root.refresh() } }
        }
    }
    DialogAction {
        text: Design.UiText.get("停止当前会话共享"); destructive: true
        onClicked: { closeConfirm.expectedEpoch = root.leaseInfo.epoch || ""; closeConfirm.expectedRevision = root.leaseInfo.revision || 0; closeConfirm.detail = ""; closeConfirm.open() }
    }
    Label { visible: root.auditState !== 0; text: Design.UiText.get("审计存储不可用或已满：新增授权将被阻止，仍可撤销共享。"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.danger }
    Label { text: Design.UiText.get("共享审计"); font.bold: true; color: Design.Theme.text }
    Label { visible: root.auditRows.length === 0; text: Design.UiText.get("此页暂无记录"); color: Design.Theme.secondary }
    Repeater {
        model: root.auditRows
        delegate: Label {
            required property var modelData
            text: "#" + modelData.seq + " · " + root.eventLabel(modelData.event) + " · " + root.phaseLabel(modelData.phase) + "\n" + modelData.time
            textFormat: Text.PlainText; color: Design.Theme.secondary; font.pixelSize: 12
            Layout.fillWidth: true; wrapMode: Text.Wrap
        }
    }
    RowLayout {
        DialogAction { text: Design.UiText.get("从头查看"); enabled: root.auditAfter !== 0; onClicked: { root.auditAfter = 0; root.refresh() } }
        DialogAction { text: Design.UiText.get("下一页记录"); enabled: root.auditNext < root.auditEnd; onClicked: { root.auditAfter = root.auditNext; root.refresh() } }
    }
    WorkbenchConfirmDialog {
        id: closeConfirm
        title: "停止当前会话共享"
        property string expectedEpoch
        property int expectedRevision
        message: "将撤销此会话的全部共享设备和待配对邀请，普通移动授权及其他会话不受影响。"
        contextText: root.sessionTitle
        primaryText: "停止共享"; destructive: true
        onPrimaryRequested: {
            const result = root.api("close", {session: root.sessionId, epoch: expectedEpoch, revision: expectedRevision})
            if (result) { closeConfirm.close(); root.sharingClosed(); root.refresh() }
            else { closeConfirm.detail = "停止共享未完成，请刷新状态后核对。"; closeConfirm.detailIsError = true }
        }
    }
}
