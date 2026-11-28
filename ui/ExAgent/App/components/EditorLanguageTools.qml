pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
RowLayout {
    id: root
    required property var editor
    property LanguageDocument service
    spacing: 5
    function settings() {
        if (!service) return
        const value = service.configuration
        serverPath.text = value.program || ""
        serverArgs.text = value.arguments || ""
        setup.open()
    }
    function completion() { if (service) service.complete(editor.cursorPosition) }
    function definition() { if (service) service.definition(editor.cursorPosition) }
    function rename() { newName.text = editor.selectedText; renameInput.open() }
    onServiceChanged: { completionPopup.close(); diagnosticsPopup.close(); definitionsPopup.close(); renamePreview.close(); renameInput.close(); setup.close() }
    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true; Layout.minimumWidth: 0
        text: root.service ? (Design.UiText.get(root.service.message) || (root.service.state === 2 ? "LSP · " + root.service.language : root.service.state === 1 ? Design.UiText.get("语言服务连接中…") : Design.UiText.get("语言服务未连接"))) : Design.UiText.get("大文件或只读模式")
        color: root.service && root.service.message ? Design.Theme.warning : Design.Theme.secondary
        font.pixelSize: 11; elide: Text.ElideRight
        ToolTip.visible: tip.containsMouse; ToolTip.text: text
        MouseArea { id: tip; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
    }
    Design.IconButton { iconName: "code"; tooltip: Design.UiText.get(Qt.platform.os === "osx" ? "补全 · ⌘Enter" : "补全 · Ctrl+Space"); implicitWidth: 28; enabled: !!root.service && root.service.state === 2; focusPolicy: Qt.NoFocus; onClicked: root.completion() }
    Design.IconButton { iconName: "right"; tooltip: Design.UiText.get("转到定义 · F12"); implicitWidth: 28; enabled: !!root.service && root.service.state === 2; focusPolicy: Qt.NoFocus; onClicked: root.definition() }
    DialogAction { text: Design.UiText.get("重命名"); enabled: !!root.service && root.service.state === 2; onClicked: root.rename() }
    DialogAction { text: Design.UiText.get("诊断") + (root.service ? " " + root.service.diagnostics.length : ""); enabled: !!root.service && root.service.diagnostics.length > 0; onClicked: diagnosticsPopup.open() }
    DialogAction { text: Design.UiText.get("语言服务"); enabled: !!root.service; onClicked: root.settings() }
    Connections {
        target: root.service
        function onCompletionsReady() { if (root.service && root.service.completions.length && root.editor.cursorPosition === root.service.completionPosition) completionPopup.open() }
        function onDefinitionsReady() { if (root.service && root.service.locations.length) definitionsPopup.open() }
        function onRenameReady() { renamePreview.open() }
    }
    Popup {
        id: completionPopup
        parent: root.editor
        width: Math.min(440, root.editor.width)
        height: Math.min(260, root.service ? root.service.completions.length * 42 + 12 : 12)
        property rect caret: root.editor.positionToRectangle(root.editor.cursorPosition)
        x: Math.max(0, Math.min(root.editor.width - width, caret.x))
        y: Math.max(0, Math.min(root.editor.height - height, caret.y + caret.height))
        padding: 6; focus: true
        background: Rectangle { radius: 8; color: Design.Theme.surface; border.color: Design.Theme.border }
        onOpened: { candidates.currentIndex=0; candidates.forceActiveFocus() }
        onClosed: root.editor.forceActiveFocus()
        contentItem: ListView {
            id: candidates
            clip: true; model: root.service ? root.service.completions : []
            keyNavigationWraps: true
            Keys.onReturnPressed: event => { if (root.service) root.service.acceptCompletion(currentIndex); completionPopup.close(); event.accepted=true }
            Keys.onTabPressed: event => { if (root.service) root.service.acceptCompletion(currentIndex); completionPopup.close(); event.accepted=true }
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: candidates.width; height: 42
                highlighted: ListView.isCurrentItem
                Accessible.name: modelData.label
                contentItem: Column {
                    Text { width: parent ? parent.width : 0; text: modelData.label; textFormat: Text.PlainText; font.family: Design.Theme.monoFont; color: Design.Theme.text; elide: Text.ElideRight }
                    Text { width: parent ? parent.width : 0; text: modelData.detail; textFormat: Text.PlainText; font.pixelSize: 11; color: Design.Theme.secondary; elide: Text.ElideRight }
                }
                onClicked: { if (root.service) root.service.acceptCompletion(index); completionPopup.close() }
            }
        }
    }
    WorkbenchDialog {
        id: setup; parent: Overlay.overlay; anchors.centerIn: parent; title: Design.UiText.get("语言服务设置")
        modal: true; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 12
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: root.service ? root.service.language : ""; color: Design.Theme.secondary }
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("使用本机 stdio LSP 服务器；点击连接后启动，配置可复用。") }
            DialogInput { id: serverPath; Layout.fillWidth: true; placeholderText: Design.UiText.get("服务器可执行文件绝对路径") }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 86
                TextArea { id: serverArgs; placeholderText: Design.UiText.get("启动参数，每项一行"); textFormat: TextEdit.PlainText; selectByMouse: true; color: Design.Theme.text; background: Rectangle { radius: 6; color: Design.Theme.surface; border.color: Design.Theme.border } }
            }
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; visible: !!root.service && !!root.service.message; text: root.service ? Design.UiText.get(root.service.message) : ""; wrapMode: Text.Wrap; color: Design.Theme.warning }
            WorkbenchDialogActions {
                Layout.fillWidth: true
                secondaryText: root.service && root.service.state !== 0 ? Design.UiText.get("断开") : ""
                primaryText: Design.UiText.get("连接")
                onSecondaryRequested: if (root.service) root.service.disconnectServer()
                onCancelled: setup.close()
                onPrimaryRequested: { if (root.service) { root.service.connectServer(serverPath.text,serverArgs.text); if (!root.service.message && (root.service.state===1 || root.service.state===2)) { setup.close(); root.editor.forceActiveFocus() } } }
            }
        }
    }
    WorkbenchDialog {
        id: diagnosticsPopup; parent: Overlay.overlay; anchors.centerIn: parent; title: Design.UiText.get("代码诊断")
        modal: true; sizeClass: WorkbenchDialog.Wide; standardButtons: Dialog.Close
        contentItem: ListView {
            implicitHeight: Math.min(380, contentHeight); clip: true
            model: root.service ? root.service.diagnostics : []
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view ? ListView.view.width : 0; implicitHeight: Math.max(46, description.implicitHeight+16)
                contentItem: Text { id: description; text: modelData.line + " · " + modelData.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: modelData.severity === 1 ? Design.Theme.danger : Design.Theme.warning }
                onClicked: { if (root.service) root.service.selectDiagnostic(index); diagnosticsPopup.close(); root.editor.forceActiveFocus() }
            }
        }
    }
    WorkbenchDialog {
        id: definitionsPopup; parent: Overlay.overlay; anchors.centerIn: parent; title: Design.UiText.get("选择定义")
        modal: true; standardButtons: Dialog.Close
        contentItem: ListView {
            implicitHeight: Math.min(320,contentHeight); clip: true; model: root.service ? root.service.locations : []
            delegate: ItemDelegate { required property var modelData; required property int index; width: ListView.view ? ListView.view.width : 0; text: modelData.label; onClicked: { if (root.service) root.service.openDefinition(index); definitionsPopup.close() } }
        }
    }
    WorkbenchDialog {
        id: renameInput; parent: Overlay.overlay; anchors.centerIn: parent; title: Design.UiText.get("重命名符号")
        modal: true; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 12
            DialogInput { id: newName; Layout.fillWidth: true; placeholderText: Design.UiText.get("新名称"); maximumLength: 256; onAccepted: { if (!text.trim()) return; if (root.service) root.service.rename(root.editor.cursorPosition,text); renameInput.close() } }
            WorkbenchDialogActions { Layout.fillWidth: true; primaryText: Design.UiText.get("预览修改"); primaryEnabled: !!newName.text.trim(); onCancelled: renameInput.close(); onPrimaryRequested: { if (root.service) root.service.rename(root.editor.cursorPosition,newName.text); renameInput.close() } }
        }
        onOpened: newName.forceActiveFocus()
    }
    WorkbenchDialog {
        id: renamePreview; parent: Overlay.overlay; anchors.centerIn: parent; title: Design.UiText.get("确认重命名修改")
        modal: true; standardButtons: Dialog.NoButton
        onRejected: if (root.service) root.service.discardRename()
        contentItem: ColumnLayout {
            spacing: 12
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("应用到编辑缓冲，可逐文件撤销；确认保存前不写入磁盘。") }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(320,contentHeight); clip: true
                model: root.service ? root.service.renamePreview : []
                delegate: Column {
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0; spacing: 4
                    Text { width: parent ? parent.width : 0; text: modelData.path + " · " + modelData.count; textFormat: Text.PlainText; color: Design.Theme.text; elide: Text.ElideMiddle }
                    Repeater {
                        model: modelData.examples
                        Text { required property var modelData; width: parent ? parent.width : 0; text: "  " + modelData.line + ": " + modelData.before + " → " + modelData.after; textFormat: Text.PlainText; color: Design.Theme.secondary; font.family: Design.Theme.monoFont; elide: Text.ElideRight }
                    }
                    Item { width: 1; height: 10 }
                }
            }
            Label { textFormat: Text.PlainText; Layout.fillWidth: true; visible: !!root.service && !!root.service.message; text: root.service ? Design.UiText.get(root.service.message) : ""; wrapMode: Text.Wrap; color: Design.Theme.warning }
            WorkbenchDialogActions {
                Layout.fillWidth: true; primaryText: Design.UiText.get("应用修改"); primaryEnabled: !!root.service && root.service.renamePreview.length>0
                onCancelled: { if (root.service) root.service.discardRename(); renamePreview.close() }
                onPrimaryRequested: { if (root.service && root.service.applyRename()) { renamePreview.close(); root.editor.forceActiveFocus() } }
            }
        }
    }
}
