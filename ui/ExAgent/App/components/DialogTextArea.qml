pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// 多行输入复用对话框调色板；长内容在字段内滚动，不推挤确认按钮。
ScrollView {
    id: field
    property alias text: editor.text
    property alias placeholderText: editor.placeholderText
    implicitHeight: 120
    contentWidth: availableWidth
    clip: true
    TextArea {
        id: editor
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        leftPadding: 11; rightPadding: 11; topPadding: 9; bottomPadding: 9
        color: palette.text; placeholderTextColor: palette.placeholderText
        background: Rectangle {
            radius: 6; color: editor.palette.base
            border.color: editor.activeFocus ? editor.palette.highlight : editor.palette.mid
        }
    }
}
