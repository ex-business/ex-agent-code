pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

RowLayout {
    id: header
    required property var group
    required property string messageId
    property var selectionManager: null
    signal toggleRequested()
    spacing: 7
    Design.Icon { name: "activity"; implicitWidth: 15; implicitHeight: 15; color: Number(header.group.status) === 2 ? Design.Theme.danger : Design.Theme.muted }
    Design.SelectableText {
        Layout.fillWidth: true
        text: header.group.summary || "工具操作"
        textFormat: TextEdit.PlainText
        font.pixelSize: 13; color: Design.Theme.secondary
        messageId: header.messageId; blockIndex: 0; selectionManager: header.visible ? header.selectionManager : null
        onTapped: header.toggleRequested()
    }
    Design.IconButton {
        iconName: Number(header.group.expansion) === 2 ? "right" : "down"
        tooltip: Number(header.group.expansion) === 2 ? "展开工具记录" : "收起工具记录"
        implicitWidth: 28; implicitHeight: 28
        onClicked: header.toggleRequested()
    }
}
