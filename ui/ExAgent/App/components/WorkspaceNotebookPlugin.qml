pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
ColumnLayout {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    spacing: 8
    WorkspaceViewerData { id: fileData; filePath: root.workspace.rootPath+"/"+root.workspace.activePath; rootPath: root.workspace.rootPath }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 10
        Label { text: "Jupyter"; color: Design.Theme.text; font.bold: true }
        Label { text: (fileData.content.cellCount || 0)+Design.UiText.get(" 个单元格 · 只读，不执行内核"); color: Design.Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Design.DialogAction { text: Design.UiText.get("刷新"); onClicked: fileData.reload() }
    }
    Design.WorkspaceViewerStatus { model: fileData; Layout.fillWidth: true; Layout.margins: visible ? 16 : 0 }
    ListView {
        id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 14; reuseItems: true
        model: fileData.content.cells || []; cacheBuffer: height
        ScrollBar.vertical: ScrollBar {width: 6}
        delegate: ColumnLayout {
            id: cell
            required property var modelData
            required property int index
            width: list.width-24; x: 12; spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: cell.modelData.kind===1 ? "In ["+(cell.modelData.execution===null ? " " : cell.modelData.execution)+"]" : cell.modelData.kind===2 ? "Markdown" : "Raw"; color: Design.Theme.secondary; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Design.DialogAction { text: Design.UiText.get("复制"); quiet: true; onClicked: fileData.copy(cell.modelData.source) }
            }
            Loader {
                Layout.fillWidth: true
                sourceComponent: cell.modelData.kind===2 ? prose : code
            }
            Component { id: code; Design.CodeBlock { width: cell.width; code: cell.modelData.source; language: cell.modelData.kind===1 ? cell.modelData.language : "text"; controller: root.controller; maximumCodeHeight: 420; blockIndex: cell.index } }
            Component {
                id: prose
                ColumnLayout {
                    width: cell.width
                    WorkspaceMarkdownModel { id: markdown; source: cell.modelData.source; filePath: root.workspace.rootPath+"/"+root.workspace.activePath; rootPath: root.workspace.rootPath; previewWidth: Math.floor(cell.width); dark: Design.Theme.dark }
                    Repeater {
                        model: markdown.blocks
                        delegate: Loader {
                            id: blockSlot; required property var modelData; Layout.fillWidth: true
                            sourceComponent: modelData.kind===1 ? fenced : text
                            Component {id: fenced; Design.CodeBlock {width: cell.width;code: blockSlot.modelData.text;language:blockSlot.modelData.language;controller:root.controller;maximumCodeHeight:420;blockIndex:cell.index}}
                            Component {id: text; Design.MarkdownText {id: paragraph;width:cell.width;block:blockSlot.modelData;suppliedPresentation:blockSlot.modelData;selectionView:list;preparation:Design.ProsePreparation {slot:paragraph;source:""} onLinkActivated: link=>root.workspace.openLink(link,root.workspace.rootPath+"/"+root.workspace.activePath)}}
                        }
                    }
                }
            }
            Repeater {
                model: cell.modelData.outputs
                delegate: Loader {
                    id: output; required property var modelData; Layout.fillWidth: true
                    sourceComponent: modelData.kind===2 ? picture : plain
                    Component {id: picture; Image {width:cell.width;height:Math.min(480,sourceSize.height>0 ? width*sourceSize.height/sourceSize.width : 200);source:output.modelData.url;asynchronous:true;fillMode:Image.PreserveAspectFit;sourceSize.width:1280;sourceSize.height:960}}
                    Component {id: plain; TextArea {width:cell.width;text:output.modelData.kind===3 ? Design.UiText.get(output.modelData.text) : output.modelData.text;textFormat:TextEdit.PlainText;readOnly:true;selectByMouse:true;wrapMode:TextEdit.Wrap;font.family:Design.Theme.monoFont;color:Design.Theme.text;background:Rectangle {radius:6;color:Design.Theme.surface}}}
                }
            }
            Rectangle {Layout.fillWidth:true;implicitHeight:1;color:Design.Theme.border}
        }
    }
}
