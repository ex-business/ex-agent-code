pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    property bool renderedMarkdown: true
    readonly property bool markdown: workspace.language === "md" && workspace.source.length < 128 * 1024 && renderedMarkdown
    property bool editing: true
    property int closeIndex: -1
    property string closeRoot: ""
    property EditorDocument closeDocument
    function finishClose(save) {
        if (root.workspace.rootPath!==closeRoot || root.workspace.documentAt(closeIndex)!==closeDocument) { closeDialog.close(); return }
        if (save && !closeDocument.save()) return
        root.workspace.closeTab(closeIndex,true)
        if (root.workspace.documentAt(closeIndex)!==closeDocument) closeDialog.close()
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            visible: !!root.workspace.document && (root.workspace.document.editable ? !root.editing : root.workspace.document.state===EditorDocument.Conflict)
            Layout.fillWidth: true; Layout.margins: 8
            Item { Layout.fillWidth: true }
            DialogAction { text: "重新载入"; visible: !!root.workspace.document && !root.workspace.document.editable && root.workspace.document.state===EditorDocument.Conflict; onClicked: root.workspace.document.reload() }
            DialogAction { text: "编辑"; visible: !!root.workspace.document && root.workspace.document.editable; enabled: !!root.workspace.document && root.workspace.document.editable; onClicked: root.editing=!root.editing }
        }
        Label { visible: !!root.workspace.document && !root.workspace.document.editable; text: root.workspace.document ? root.workspace.document.reason : ""; wrapMode: Text.Wrap; color: Design.Theme.secondary; Layout.fillWidth: true; Layout.margins: 12 }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            active: root.visible && !!root.workspace.document
            sourceComponent: root.editing && root.workspace.document && root.workspace.document.editable ? editable : readOnlyPreview
        }
    }
    Component {
        id: editable
        Design.WorkspaceEditor { document: root.workspace.document; onPreviewRequested: root.editing=false }
    }
    Component {
        id: readOnlyPreview
    Flickable {
        id: preview
        property bool pixelWheelEnabled: true
        property bool pixelWheelAnimated: Design.Theme.motionEnabled
        readonly property bool pixelWheelMoving: wheel.kineticRunning
        function pixelScrollBy(x: real, y: real): point { return Qt.point(0, wheel.consumePixels(y)) }
        signal pixelScrollStarted()
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 32
        acceptedButtons: Qt.NoButton
        boundsBehavior: Flickable.StopAtBounds
        Design.SmoothWheel { id: wheel; view: preview }
        ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: content
            x: 16; y: 16; width: preview.width - 32
            spacing: 14
            Repeater {
                model: root.markdown ? MarkdownFormatter.format(root.workspace.source, Design.Theme.dark)
                                     : [{kind: 1, text: root.workspace.source, language: root.workspace.language}]
                delegate: Loader {
                    id: blockLoader
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    sourceComponent: modelData.kind === 1 ? code : prose
                    Component { id: code; Design.CodeBlock { filePath: root.workspace.activePath; blockIndex: blockLoader.index; code: blockLoader.modelData.text; language: blockLoader.modelData.language; controller: root.controller; maximumCodeHeight: Math.max(160, root.height - 90); outerWheel: wheel } }
                    Component {
                        id: prose
                        Design.MarkdownText {
                            id: previewProse
                            block: blockLoader.modelData
                            selectionView: preview
                            preparation: Design.ProsePreparation { slot: previewProse; source: previewProse.block.text || "" }
                            onLinkActivated: (link) => { if (link.startsWith("file:")) root.workspace.open(link) }
                        }
                    }
                }
            }
        }
    }
    }
    Connections {
        target: root.workspace
        function onCloseRequested(index: int) {
            root.closeIndex=index; root.closeRoot=root.workspace.rootPath; root.closeDocument=root.workspace.documentAt(index)
            closeDialog.open()
        }
    }
    WorkbenchConfirmDialog {
        id: closeDialog
        title: Design.UiText.get("关闭前保存修改？")
        contextText: root.closeDocument ? root.closeDocument.path : ""
        detail: root.closeDocument ? root.closeDocument.error : ""
        detailIsError: !!detail
        primaryText: "保存并关闭"
        secondaryText: "放弃修改"
        secondaryDestructive: true
        onPrimaryRequested: root.finishClose(true)
        onSecondaryRequested: root.finishClose(false)
    }
}
