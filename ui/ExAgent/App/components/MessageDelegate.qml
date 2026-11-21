pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Item {
    id: root
    required property ChatController controller
    required property int rowIndex
    required property int messageKind
    required property string content
    required property string messageTitle
    required property int messageStatus
    required property bool expanded
    required property var contentBlocks
    property var outerWheel: null
    readonly property bool isUser: messageKind === MessageListModel.User
    readonly property bool isTool: messageKind === MessageListModel.Tool
    readonly property bool isNotice: messageKind === MessageListModel.Notice
    implicitHeight: body.implicitHeight + (isTool ? (messageTitle === "文件修改" ? 47 : 8) : isUser ? 45 : 26)

    // 每条消息只实例化自己的类型，不能用 visible=false 留下其他类型的全文布局。
    Loader {
        id: body
        width: root.width
        sourceComponent: root.isUser ? userBody : root.isNotice ? noticeBody
                         : root.isTool ? (root.messageTitle === "文件修改" ? filesBody : toolBody) : assistantBody
    }
    Component {
        id: userBody
        Item {
            FontMetrics { id: userMetrics; font: userText.font }
            implicitHeight: bubble.implicitHeight
            Rectangle {
                id: bubble
                anchors.right: parent.right
                // 不查询已换行 TextEdit 的 implicitWidth，避免自然宽度和固定宽度反复排版。
                width: Math.min(Math.ceil(userMetrics.boundingRect(root.content).width) + 30, root.width * 0.85)
                implicitHeight: userText.implicitHeight + 22
                height: implicitHeight
                radius: 18
                color: Design.Theme.dark ? "#eeeeee" : "#000000"
                TextEdit {
                    id: userText
                    width: parent.width - 30
                    x: 15; y: 11
                    text: root.content
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    color: Design.Theme.dark ? "#202020" : "#ffffff"
                    font.family: Design.Theme.font
                    font.pixelSize: 14
                    Accessible.name: "用户消息"
                }
            }
        }
    }
    Component {
        id: noticeBody
        RowLayout {
            spacing: 8
            Text { text: "ⓘ"; color: Design.Theme.muted; font.pixelSize: 13; Layout.alignment: Qt.AlignTop }
            Text {
                Layout.fillWidth: true
                text: root.content
                wrapMode: Text.Wrap
                color: Design.Theme.muted
                font.pixelSize: 12
                lineHeight: 1.4
            }
        }
    }
    Component {
        id: filesBody
        Design.ChangedFilesCard {}
    }
    Component {
        id: toolBody
        ColumnLayout {
            spacing: 5
            AbstractButton {
                id: toolButton
                Layout.fillWidth: true
                implicitHeight: 30
                hoverEnabled: true
                Accessible.name: root.messageTitle + (root.expanded ? "，收起" : "，展开")
                onClicked: root.controller.messages.toggleExpanded(root.rowIndex)
                background: Rectangle { color: toolButton.hovered ? Design.Theme.hover : "transparent"; radius: 6 }
                contentItem: RowLayout {
                    spacing: 8
                    Design.Icon { name: "terminal"; implicitWidth: 15; implicitHeight: 15 }
                    Text { text: root.messageTitle; color: Design.Theme.secondary; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                    Design.Icon { name: root.expanded ? "down" : "right"; implicitWidth: 13; implicitHeight: 13 }
                }
            }
            Loader {
                active: root.expanded
                Layout.fillWidth: true
                sourceComponent: Design.CodeBlock { code: root.content; language: "输出"; controller: root.controller; outerWheel: root.outerWheel }
            }
        }
    }
    Component {
        id: assistantBody
        ColumnLayout {
            spacing: 14
            Repeater {
                model: root.contentBlocks
                delegate: Loader {
                    id: blockLoader
                    required property var modelData
                    Layout.fillWidth: true
                    sourceComponent: blockLoader.modelData.kind === 1 ? codeView : proseView
                    Component {
                        id: proseView
                        Item {
                            implicitWidth: root.width
                            implicitHeight: prose.implicitHeight
                            // Loader 只测量容器，正文 TextEdit 只按固定宽度布局一次。
                        TextEdit {
                            id: prose
                            width: parent.width
                            text: blockLoader.modelData.html
                            textFormat: TextEdit.RichText
                            wrapMode: TextEdit.Wrap
                            readOnly: true
                            selectByMouse: true
                            color: Design.Theme.text
                            selectionColor: Design.Theme.selected
                            font.family: Design.Theme.font
                            font.pixelSize: 14
                            Accessible.name: "助手消息"
                            onLinkActivated: (link) => {
                                if (link.startsWith("https://") || link.startsWith("http://")) Qt.openUrlExternally(link)
                            }
                        }
                        }
                    }
                    Component {
                        id: codeView
                        Design.CodeBlock { code: blockLoader.modelData.text; language: blockLoader.modelData.language; controller: root.controller; outerWheel: root.outerWheel }
                    }
                }
            }
            RowLayout {
                spacing: 1
                Design.IconButton {
                    implicitWidth: 26; implicitHeight: 26
                    iconName: copiedTimer.running ? "check" : "copy"
                    tooltip: "复制回复"
                    onClicked: { root.controller.copyText(root.content); copiedTimer.restart() }
                }
                Design.Icon { name: "speaker"; implicitWidth: 16; implicitHeight: 16; color: Design.Theme.muted }
                Design.Icon { name: "dots"; implicitWidth: 22; implicitHeight: 16; color: Design.Theme.muted }
            }
        }
    }
    Timer { id: copiedTimer; interval: 1800 }
}
