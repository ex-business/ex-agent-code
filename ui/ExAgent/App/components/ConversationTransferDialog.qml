pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat

WorkbenchDialog {
    id: root
    required property ChatController controller
    title: "整理会话"
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 32 : 460)
    modal: true
    property string sourceId
    property var turns: []
    property var destinations: []
    property var summary: ({})
    property bool prepared: false
    readonly property int mode: action.currentValue ?? ConversationTransfer.Send
    readonly property bool creates: mode === ConversationTransfer.Fork || mode === ConversationTransfer.Extract
    function begin() {
        sourceId = controller.currentSessionId
        turns = controller.transfer.turns(sourceId)
        destinations = controller.projects.rows.filter(r => r.kind === ProjectNavigator.SessionRow
            && r.project === controller.currentProjectId && r.session !== sourceId)
        action.currentIndex = 0
        target.currentIndex = destinations.length ? 0 : -1
        first.currentIndex = 0
        last.currentIndex = turns.length - 1
        prepared = false
        open()
    }
    function preview() {
        summary = controller.transfer.preview(mode, sourceId, target.currentValue || "", first.currentIndex, last.currentIndex)
        prepared = true
    }
    contentItem: ColumnLayout {
        spacing: 10
        DialogSelect {
            id: action; Layout.fillWidth: true
            Accessible.name: "操作"; textRole: "name"; valueRole: "mode"
            model: [{name: "从此处分支", mode: ConversationTransfer.Fork}, {name: "拆出所选回合", mode: ConversationTransfer.Extract},
                {name: "发送到会话", mode: ConversationTransfer.Send}, {name: "合并到会话", mode: ConversationTransfer.Merge}]
            onCurrentIndexChanged: root.prepared = false
        }
        DialogSelect {
            id: target; visible: !root.creates; Layout.fillWidth: true
            Accessible.name: "目标会话"; model: root.destinations; textRole: "title"; valueRole: "session"
            displayText: currentIndex < 0 ? "同项目下尚无其他会话" : currentText
            onCurrentIndexChanged: root.prepared = false
        }
        Label { visible: root.mode !== ConversationTransfer.Fork; text: "起始回合" }
        DialogSelect {
            id: first; visible: root.mode !== ConversationTransfer.Fork; Layout.fillWidth: true
            Accessible.name: "起始回合"; model: root.turns
            onCurrentIndexChanged: root.prepared = false
        }
        Label { text: "截至回合" }
        DialogSelect {
            id: last; Layout.fillWidth: true
            Accessible.name: "截至回合"; model: root.turns
            onCurrentIndexChanged: root.prepared = false
        }
        Label {
            visible: root.prepared; Layout.fillWidth: true; wrapMode: Text.Wrap
            text: root.summary.error || ("目标：" + (root.summary.target || "") + "　新增 " + (root.summary.added || 0)
                                         + " 条，已存在 " + (root.summary.duplicates || 0) + " 条")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            DialogAction { text: Design.UiText.get("取消"); onClicked: root.close() }
            DialogAction {
                text: root.prepared && root.summary.ready ? "确认" : "预览"
                enabled: root.turns.length > 0 && (root.creates || target.currentIndex >= 0)
                onClicked: {
                    if (!root.prepared || !root.summary.ready) root.preview()
                    else if (root.controller.transfer.commit()) root.close()
                    else root.prepared = false
                }
            }
        }
    }
}
