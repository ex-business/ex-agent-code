pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: panel
    required property ChatController controller
    property string session
    property string run
    property string token
    property string requestDetail
    // 打开时固定原请求。后台撤回或切换会话时关闭弹窗，不能顺延到下一项。
    function present(expected: string) {
        const request = controller.agent.approval
        if (!expected || request.token !== expected ||
                !controller.agent.requestPending(controller.currentSessionId, request.runId, expected)) return
        session = controller.currentSessionId
        run = request.runId
        token = expected
        requestDetail = request.detail
        open()
    }
    function valid(): bool {
        return !controller.previewMode && session === controller.currentSessionId && controller.agent.requestPending(session, run, token)
    }
    function decide(allow: bool) {
        if (valid()) controller.agent.decide(token, allow)
        close()
    }
    Connections {
        target: panel.controller.agent
        function onChanged() { if (panel.visible && !panel.valid()) panel.close() }
    }
    parent: Overlay.overlay
    title: "工具审批"; modal: true; anchors.centerIn: Overlay.overlay
    sizeClass: Design.WorkbenchDialog.Wide; height: Math.min(380, Overlay.overlay.height - 32)
    contentItem: ScrollView {
        TextArea { text: panel.requestDetail; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap }
    }
    footer: Design.WorkbenchDialogActions {
        primaryText: "允许此次操作"; secondaryText: "拒绝此次操作"; secondaryDestructive: true
        onPrimaryRequested: panel.decide(true)
        onSecondaryRequested: panel.decide(false)
        onCancelled: panel.close()
    }
}
