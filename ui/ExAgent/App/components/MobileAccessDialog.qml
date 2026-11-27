pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    objectName: "mobileAccess"
    required property ChatController controller
    title: "移动接入"
    anchors.centerIn: Overlay.overlay; modal: true; sizeClass: WorkbenchDialog.Wide; standardButtons: Dialog.Close
    property string errorText
    property string origin
    property string offerUrl
    property string sessionId
    property string sessionTitle
    property var devices: []
    function syncAppearance() { if (controller.mobile) controller.mobile.setAppearance(Design.Theme.tokens, Design.UiText.catalog, AppearanceRegistry.language) }
    Component.onCompleted: syncAppearance()
    Connections { target: Design.Theme; function onTokensChanged() { root.syncAppearance() } }
    Connections { target: Design.UiText; function onCatalogChanged() { root.syncAppearance() } }
    function api(action, params) {
        const value = controller.mobile.operate(action, params || {})
        if (!value.ok) errorText = value.error
        return value.ok ? value.result : null
    }
    function refresh() {
        const status = api("status"), list = api("devices")
        if (status) origin = status.origin || ""
        if (list) devices = list.items
    }
    function begin() { sessionId = controller.currentSessionId; sessionTitle = controller.sessionTitle; errorText = ""; offerUrl = ""; refresh(); open() }
    Timer { interval: 1500; repeat: true; running: root.visible; onTriggered: root.refresh() }
    contentItem: ScrollView {
        implicitHeight: Math.min(root.maximumBodyHeight, form.implicitHeight, 520)
        contentWidth: availableWidth; clip: true
        ColumnLayout {
            id: form; width: root.availableWidth; spacing: 12
            Label { text: Design.UiText.get("手机仅访问你授权的单个会话，桌面继续持有执行与权限。"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
            RowLayout {
                Layout.fillWidth: true
                DialogInput { id: address; Layout.fillWidth: true; text: "127.0.0.1"; placeholderText: Design.UiText.get("本机 IPv4 地址") }
                DialogInput { id: port; Layout.preferredWidth: 100; text: "8765"; inputMethodHints: Qt.ImhDigitsOnly; placeholderText: Design.UiText.get("端口") }
            }
            DialogInput { id: certificate; Layout.fillWidth: true; placeholderText: Design.UiText.get("TLS 证书 PEM 绝对路径（本机预览可留空）") }
            DialogInput { id: key; Layout.fillWidth: true; placeholderText: Design.UiText.get("TLS 私钥 PEM 绝对路径，仅本人可读写") }
            Label { text: Design.UiText.get("127.0.0.1 仅供本机浏览器预览。手机访问需填写局域网地址及浏览器信任的 TLS 证书。"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
            RowLayout {
                DialogAction {
                    text: Design.UiText.get(root.origin ? "重新启动接入" : "启动接入"); primary: true
                    onClicked: { root.errorText = ""; root.offerUrl = ""; if (root.api("start", {address: address.text, port: Number(port.text), certificate: certificate.text, key: key.text})) root.refresh() }
                }
                DialogAction { text: Design.UiText.get("停止接入"); enabled: !!root.origin; onClicked: { root.api("stop"); root.offerUrl = ""; root.refresh() } }
            }
            Label { text: root.origin; visible: !!text; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: Design.UiText.get("配对会话") + " · " + root.sessionTitle; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                DialogSelect { id: scope; Layout.fillWidth: true; model: [Design.UiText.get("只读查看"), Design.UiText.get("查看并发送消息")] }
                DialogAction { text: Design.UiText.get("生成配对链接"); enabled: !!root.origin && !!root.sessionId; onClicked: { root.errorText = ""; const value = root.api("offer", {session: root.sessionId, scope: scope.currentIndex + 1}); if (value) root.offerUrl = value.url } }
            }
            Design.SelectableText { Layout.fillWidth: true; visible: !!root.offerUrl; text: root.offerUrl; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: Design.Theme.text }
            Label { visible: !!root.offerUrl; text: Design.UiText.get("链接含一次性配对码，5 分钟内有效。只交给你要授权的设备。"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.secondary }
            Label { text: root.errorText; visible: !!text; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.danger }
            Repeater {
                model: root.devices
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.name + " · " + Design.UiText.get(modelData.scope === 1 ? "只读" : "允许发送") + " · " + Design.UiText.get(modelData.state === 1 ? "已授权" : "已撤销"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    DialogAction { text: Design.UiText.get("撤销授权"); enabled: modelData.state === 1; onClicked: { root.api("revoke", {id: modelData.id}); root.refresh() } }
                }
            }
        }
    }
}
