pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat

Dialog {
    parent: Overlay.overlay
    id: root
    required property ChatController controller
    property string token
    title: "工具审批"; modal: true; anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay.width - 32); height: Math.min(380, Overlay.overlay.height - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: {
        token = root.controller.agent.approval.token
        detail.text = root.controller.agent.approval.detail
        standardButton(Dialog.Ok).text = "允许此次操作"
    }
    onAccepted: root.controller.agent.decide(token,true)
    contentItem: ScrollView {
        TextArea { id: detail; textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap }
    }
}
