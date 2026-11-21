pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Item {
    id: root
    signal promptSelected(string prompt)
    signal previewRequested()
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        spacing: 14
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 48; implicitHeight: 48; radius: 14
            color: Design.Theme.surface
            Text { anchors.centerIn: parent; text: "ex"; color: Design.Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold; font.letterSpacing: -1 }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 9
            text: "一起把想法变成代码"
            color: Design.Theme.text
            font.family: Design.Theme.font
            font.pixelSize: 26
            font.weight: Font.DemiBold
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "从一个问题、一段代码，或一个新想法开始。"
            color: Design.Theme.secondary
            font.pixelSize: 13
        }
        RowLayout {
            Layout.topMargin: 19
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            Repeater {
                model: ["解释项目结构", "帮我检查代码", "一起设计新功能"]
                delegate: Button {
                    id: suggestion
                    required property string modelData
                    text: modelData
                    onClicked: root.promptSelected(modelData)
                    background: Rectangle { radius: 10; color: suggestion.hovered ? Design.Theme.hover : "transparent"; border.color: Design.Theme.border }
                    contentItem: Text { text: suggestion.text; color: Design.Theme.secondary; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
                    padding: 12
                }
            }
        }
        Button {
            id: previewButton
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            text: "预览消息样式  →"
            flat: true
            onClicked: root.previewRequested()
            contentItem: Text { text: previewButton.text; color: previewButton.hovered ? Design.Theme.text : Design.Theme.muted; font.pixelSize: 12 }
            background: Item {}
        }
    }
}
