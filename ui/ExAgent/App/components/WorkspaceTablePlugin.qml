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
    property int selectedRow: -1
    property int selectedColumn: -1
    property string selectedText: ""
    spacing: 8
    WorkspaceViewerData {
        id: fileData
        filePath: root.workspace.rootPath+"/"+root.workspace.activePath; rootPath: root.workspace.rootPath
        onFound: (row,column) => { if(row>=0) { root.selectedRow=row; root.selectedColumn=column; root.selectedText=fileData.cellValue(row,column); grid.positionViewAtCell(Qt.point(column,row),TableView.Contain) } }
    }
    Connections { target: fileData; function onChanged() { root.selectedRow=-1;root.selectedColumn=-1;root.selectedText="" } }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 10; spacing: 8
        Label { text: "CSV / TSV"; color: Design.Theme.text; font.bold: true }
        Label { text: (fileData.content.rowCount || 0)+Design.UiText.get(" 行"); color: Design.Theme.secondary }
        Item { Layout.fillWidth: true }
        Design.DialogAction { text: Design.UiText.get("复制单元格"); enabled: root.selectedRow>=0; onClicked: fileData.copy(root.selectedText) }
        Design.DialogAction { text: Design.UiText.get("刷新"); onClicked: fileData.reload() }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
        Design.DialogInput { id: query; Layout.fillWidth: true; placeholderText: Design.UiText.get("查找单元格内容"); onAccepted: fileData.find(text) }
        Design.DialogAction { text: Design.UiText.get("查找"); onClicked: fileData.find(query.text) }
    }
    Design.WorkspaceViewerStatus { model: fileData; Layout.fillWidth: true; Layout.margins: visible ? 16 : 0 }
    HorizontalHeaderView {
        id: header; visible: fileData.state===WorkspaceViewerData.Ready
        Layout.fillWidth: true; syncView: grid; clip: true
        delegate: Rectangle {
            required property int column
            implicitWidth: 180; implicitHeight: 30; color: Design.Theme.surface
            Text { anchors.centerIn: parent; text: parent.column+1; color: Design.Theme.secondary; font.pixelSize: 11 }
        }
    }
    TableView {
        id: grid; visible: fileData.state===WorkspaceViewerData.Ready
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        columnSpacing: 1; rowSpacing: 1; model: fileData.table; reuseItems: true
        columnWidthProvider: column => 180
        delegate: Rectangle {
            id: cell
            required property int row
            required property int column
            required property string display
            implicitWidth: 180; implicitHeight: 36
            border.width: root.selectedRow===row && root.selectedColumn===column ? 1 : 0; border.color: Design.Theme.accent
            color: root.selectedRow===row && root.selectedColumn===column ? Design.Theme.hover : row%2 ? Design.Theme.surface : Design.Theme.background
            Text { anchors.fill: parent; anchors.margins: 8; text: cell.display.replace(/\r?\n/g," ↵ "); textFormat: Text.PlainText; elide: Text.ElideRight; color: Design.Theme.text; font.pixelSize: 12 }
            TapHandler { onTapped: { root.selectedRow=cell.row;root.selectedColumn=cell.column;root.selectedText=cell.display } }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
        ScrollBar.vertical: ScrollBar { width: 6 }
        ScrollBar.horizontal: ScrollBar { height: 6 }
    }
    TextArea {
        Layout.fillWidth: true; Layout.preferredHeight: Math.min(120,implicitHeight); Layout.leftMargin: 10; Layout.rightMargin: 10
        visible: root.selectedRow>=0; text: root.selectedText; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
        color: Design.Theme.text; background: Rectangle { radius: 6; color: Design.Theme.surface }
    }
}
