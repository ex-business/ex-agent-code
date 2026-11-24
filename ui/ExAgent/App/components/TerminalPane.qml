pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Rectangle {
    id: root
    required property string terminalId
    required property var backend
    property string caption: ""
    readonly property bool supported: surface.supported
    function focusTerminal() { surface.forceActiveFocus() }
    color: "#191d24"; radius: 9
    border.color: "#323844"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        RowLayout {
            Layout.leftMargin: 11; Layout.rightMargin: 6; Layout.fillWidth: true; Layout.preferredHeight: 32
            Design.Icon { name: "terminal"; color: "#9ca8b9"; implicitWidth: 15; implicitHeight: 15 }
            Text { text: root.caption; color: "#c6ced9"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle }
            Design.IconButton { text: "查找"; tooltip: "查找终端回滚"; iconName: "search"; iconColor: "#a5afbd"; iconSize: 15; implicitWidth: 26; implicitHeight: 25; onClicked: history.open() }
        }
        Rectangle { color: "#2b313b"; Layout.fillWidth: true; implicitHeight: 1 }
        TerminalSurface {
            id: surface
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 8
            target: root.visible ? root.terminalId : ""
            ToolTip { id: pasteNotice; timeout: 3000 }
            onContextMenuRequested: (x,y) => { actions.x=x; actions.y=y; actions.open() }
            Design.ContextMenu {
                id: actions
                Design.ContextMenuItem { text: "复制选中内容"; onTriggered: surface.copySelection() }
                Design.ContextMenuItem { text: "粘贴"; onTriggered: surface.pasteClipboard() }
                Design.ContextMenuItem { text: "打开链接"; visible: !!surface.contextLink; onTriggered: surface.openLink() }
                Design.ContextMenuItem { text: "复制链接"; visible: !!surface.contextLink; onTriggered: surface.copyLink() }
            }
            onPasteRejected: reason => pasteNotice.show(reason)
        }
        Label { visible: !!surface.error; text: surface.error; color: "#efaa92"; Layout.leftMargin: 8 }
    }
    TerminalHistoryDialog { id: history; backend: root.backend; terminalId: root.terminalId; parent: Overlay.overlay }
}
