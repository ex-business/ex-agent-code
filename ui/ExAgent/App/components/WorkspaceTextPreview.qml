pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Flickable {
    id: preview
    required property WorkspaceController workspace
    required property ChatController controller
    property bool markdown: false
    property string sourceText: workspace.source
    property bool pixelWheelEnabled: true
    property bool pixelWheelAnimated: Design.Theme.motionEnabled
    readonly property bool pixelWheelMoving: wheel.kineticRunning
    function pixelScrollBy(x: real,y: real): point { return Qt.point(0,wheel.consumePixels(y)) }
    signal pixelScrollStarted()
    clip: true; contentWidth: width; contentHeight: content.implicitHeight+32
    acceptedButtons: Qt.NoButton; boundsBehavior: Flickable.StopAtBounds
    Design.SmoothWheel { id: wheel; view: preview }
    ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
    WorkspaceMarkdownModel {
        id: prepared
        active: preview.visible && preview.markdown
        source: preview.markdown ? preview.sourceText : ""
        rootPath: preview.workspace.rootPath
        filePath: preview.workspace.rootPath+"/"+preview.workspace.activePath
        dark: Design.Theme.dark
        previewWidth: Math.floor(Math.max(16,preview.width-32))
    }
    ColumnLayout {
        id: content
        x: 16; y: 16; width: Math.max(1,preview.width-32); spacing: 14
        Label { visible: preview.markdown && !!prepared.error; text: Design.UiText.get(prepared.error); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.warning }
        Repeater {
            model: preview.markdown && prepared.state!==WorkspaceMarkdownModel.Failed ? prepared.blocks : [{kind:1,text:preview.sourceText,language:preview.workspace.language}]
            delegate: Loader {
                id: slot
                required property var modelData
                required property int index
                Layout.fillWidth: true
                sourceComponent: modelData.kind===1 ? code : prose
                Component { id: code; Design.CodeBlock { filePath: preview.workspace.activePath; blockIndex: slot.index; code: slot.modelData.text; language: slot.modelData.language; controller: preview.controller; maximumCodeHeight: Math.max(160,preview.height-90); outerWheel: wheel } }
                Component {
                    id: prose
                    Design.MarkdownText {
                        id: paragraph
                        block: slot.modelData; selectionView: preview
                        suppliedPresentation: slot.modelData
                        preparation: Design.ProsePreparation { slot: paragraph; source: "" }
                        onLinkActivated: link => preview.workspace.openLink(link,preview.workspace.rootPath+"/"+preview.workspace.activePath)
                    }
                }
            }
        }
    }
}
