pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

WorkbenchDialog {
    id: panel
    required property var chat
    property string group: ""
    property string groupName: ""
    property var members: []
    property string member: ""
    property string memberName: ""
    property int affected: 0
    property string error: ""
    function reload() {
        const r=chat.projectGroups.operate("list")
        if(!r.ok){error=r.error;return}
        members=r.result.items.filter(p=>p.group===group)
    }
    function present(id,title) {group=id;groupName=title;error="";reload();open()}
    function remove(id,title) {
        member=id;memberName=title
        affected=chat.projects.rows.filter(r=>r.kind===ProjectNavigator.WorkspaceRow&&r.project===group&&r.launch?.taskSource===id).length
        replacement.model=members.filter(p=>p.id!==id)
        removeError.text="";removeDialog.open()
    }
    title: "管理项目组"
    modal: true; anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Normal
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: panel.groupName; font.bold: true; textFormat: Text.PlainText; color: Design.Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
        Label { Layout.fillWidth: true; text: Design.UiText.get("移除只解除成员关系，项目、仓库和会话继续保留。"); wrapMode: Text.Wrap; color: Design.Theme.secondary }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(240,Math.max(48,contentHeight)); clip: true
            model: panel.members; spacing: 4; ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width; height: 40
                Label { Layout.fillWidth: true; text: parent.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: Design.Theme.text }
                DialogAction { text: Design.UiText.get("移除成员…"); onClicked: panel.remove(parent.modelData.id,parent.modelData.name) }
            }
        }
        Label { visible: !!panel.error; Layout.fillWidth: true; wrapMode: Text.Wrap; text: panel.error; textFormat: Text.PlainText; color: Design.Theme.text }
    }
    footer: WorkbenchDialogActions {
        primaryText: "完成"; secondaryText: "删除项目组…"; secondaryDestructive: true
        onCancelled: panel.reject()
        onPrimaryRequested: panel.close()
        onSecondaryRequested: { deletion.detailIsError=false;deletion.detail="";deletion.open() }
    }
    WorkbenchDialog {
        id: removeDialog
        title: "移除项目组成员"; modal: true; anchors.centerIn: Overlay.overlay
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: panel.memberName; textFormat: Text.PlainText; Layout.fillWidth: true; color: Design.Theme.text }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("成员将转为独立项目，已有会话与工作树保持原归属。"); color: Design.Theme.secondary }
            Label { visible: panel.affected>0; text: Design.UiText.get("替代任务来源仓库"); color: Design.Theme.text }
            DialogSelect { id: replacement; visible: panel.affected>0; Layout.fillWidth: true; textRole: "name"; valueRole: "id" }
            Label { visible: panel.affected>0 && replacement.count===0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: Design.UiText.get("没有可替代的仓库。可先取消，再删除项目组并保留全部工作区。"); color: Design.Theme.secondary }
            Label { id: removeError; visible: !!text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Design.Theme.text }
        }
        footer: WorkbenchDialogActions {
            primaryText: "移除成员"; destructive: true
            primaryEnabled: panel.affected===0 || replacement.currentIndex>=0
            onCancelled: removeDialog.reject()
            onPrimaryRequested: {
                const r=panel.chat.projectGroups.operate("remove-member",{group:panel.group,member:panel.member,replacement:panel.affected>0?replacement.currentValue:""})
                if(!r.ok){removeError.text=r.error;return}
                removeDialog.close();panel.reload()
            }
        }
    }
    WorkbenchConfirmDialog {
        id: deletion
        title: "删除项目组"; primaryText: "删除项目组"; destructive: true
        message: "仅删除分组关系。成员转为独立项目；组内工作区和会话保留为独立项目。仓库文件、分支和附件不会删除。"
        contextText: panel.groupName
        onPrimaryRequested: {
            const r=panel.chat.projectGroups.operate("delete",{group:panel.group})
            if(!r.ok){detailIsError=true;detail=r.error;return}
            close();panel.close()
        }
    }
}
