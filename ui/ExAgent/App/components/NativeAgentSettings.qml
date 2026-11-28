pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
WorkbenchDialog {
 id: root
 required property ChatController controller
 readonly property var agent: controller.agent
 title: Design.UiText.get("原生 Agent 设置")
 modal: true; anchors.centerIn: Overlay.overlay; sizeClass: WorkbenchDialog.Compact
 property string feedback: ""
 onOpened: { const value=agent.nativeSettings; program.text=value.program || ""; modelName.text=value.model || ""; access.currentIndex=value.fullAccess ? 2 : value.write ? 1 : 0; feedback="" }
 Connections { target: root.agent; function onFailure(reason: string){if(root.visible)root.feedback=reason} }
 contentItem: ColumnLayout {
   spacing: 12
   Label { text: root.agent.terminalAgent; font.bold: true }
   Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("使用原会话与本机 CLI 登录；原生聊天和终端互斥输入，不重放中断任务。") }
   DialogInput { id: program; Layout.fillWidth: true; placeholderText: Design.UiText.get("CLI 可执行文件绝对路径") }
   DialogInput { id: modelName; Layout.fillWidth: true; placeholderText: Design.UiText.get("模型（留空沿用 CLI 会话）") }
   DialogSelect { id: access; Layout.fillWidth: true; translateOptions: true; model: ["只读", "工作区写入，每次审批", "完全访问"] }
   Label { visible: !!root.feedback; text: root.feedback; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.warningBorder }
 }
 footer: WorkbenchDialogActions {
   primaryText: "启用原生聊天"; primaryEnabled: !!program.text.trim() && !root.agent.busy
   onPrimaryRequested: if(root.agent.configureNative(program.text.trim(),modelName.text,access.currentIndex>0,access.currentIndex===2))root.accept()
   onCancelled: root.reject()
 }
}
