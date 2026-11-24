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
    property int page: 0
    property real zoom: 1
    property bool showText: false
    spacing: 8
    WorkspaceViewerData {
        id: fileData; filePath: root.workspace.rootPath+"/"+root.workspace.activePath; rootPath: root.workspace.rootPath
        onChanged: { if(state===WorkspaceViewerData.Ready) {root.page=0;readPdfText(0)} }
    }
    onPageChanged: { fileData.readPdfText(page); canvas.contentX=0;canvas.contentY=0 }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 10; spacing: 6
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "‹"; enabled: root.page>0; onClicked: root.page--; Accessible.name: Design.UiText.get("上一页") }
        Design.DialogInput { text: String(root.page+1); Layout.preferredWidth: 44; validator: IntValidator { bottom: 1; top: fileData.content.pageCount || 1 } onAccepted: root.page=Math.max(0,Math.min((fileData.content.pageCount || 1)-1,Number(text)-1)) }
        Label { text: "/ "+(fileData.content.pageCount || 0); color: Design.Theme.secondary }
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "›"; enabled: root.page+1<(fileData.content.pageCount || 0); onClicked: root.page++; Accessible.name: Design.UiText.get("下一页") }
        Item { Layout.fillWidth: true }
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "−"; onClicked: root.zoom=Math.max(0.25,root.zoom-0.25) }
        Design.DialogAction { visible: root.width>440; text: Math.round(root.zoom*100)+"%"; onClicked: root.zoom=1 }
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "+"; onClicked: root.zoom=Math.min(4,root.zoom+0.25) }
        Design.DialogAction { text: Design.UiText.get("文字"); onClicked: root.showText=!root.showText }
    }
    Design.WorkspaceViewerStatus { model: fileData; Layout.fillWidth: true; Layout.margins: visible ? 16 : 0 }
    SplitView {
        Layout.fillWidth: true; Layout.fillHeight: true; orientation: Qt.Vertical; visible: fileData.state===WorkspaceViewerData.Ready
        Flickable {
            id: canvas; SplitView.fillHeight: true; clip: true; boundsBehavior: Flickable.StopAtBounds
            contentWidth: pageImage.width; contentHeight: pageImage.height
            readonly property var dimensions: (fileData.content.pages || [])[root.page] || {width:612,height:792}
            Image {
                id: pageImage
                width: Math.max(1,canvas.width-24)*root.zoom
                height: width*canvas.dimensions.height/Math.max(1,canvas.dimensions.width)
                source: fileData.content.token ? "image://workspace-pdf/"+fileData.content.token+"/"+root.page : ""
                sourceSize.width: Math.min(4096,Math.round(width*Screen.devicePixelRatio)); sourceSize.height: Math.min(4096,Math.round(height*Screen.devicePixelRatio))
                asynchronous: true; cache: false; fillMode: Image.PreserveAspectFit
            }
            ScrollBar.vertical: ScrollBar { width: 6 }
            ScrollBar.horizontal: ScrollBar { height: 6 }
        }
        ScrollView {
            visible: root.showText; SplitView.preferredHeight: 180; SplitView.minimumHeight: visible ? 80 : 0
            TextArea { text: fileData.pageText; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Design.Theme.text; background: Rectangle {color: Design.Theme.surface} }
        }
    }
}
