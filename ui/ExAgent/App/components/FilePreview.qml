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
    Flickable {
        id: preview
        property bool pixelWheelEnabled: true
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
                    Layout.fillWidth: true
                    sourceComponent: modelData.kind === 1 ? code : prose
                    Component { id: code; Design.CodeBlock { code: blockLoader.modelData.text; language: blockLoader.modelData.language; controller: root.controller; maximumCodeHeight: Math.max(160, root.height - 90); outerWheel: wheel } }
                    Component { id: prose; Design.MarkdownText { block: blockLoader.modelData; onLinkActivated: (link) => { if (link.startsWith("file:")) root.workspace.open(link) } } }
                }
            }
        }
    }
    Connections { target: root.workspace; function onPreviewChanged() { preview.contentY = 0 } }
}
