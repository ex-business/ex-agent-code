pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

ColumnLayout {
    id: root
    required property GitChanges changes
    property string folder
    spacing: 4
    onFolderChanged: {
        scope.currentIndex = GitChanges.Local
        from.text = "HEAD"
        to.text = "HEAD"
        applyRange()
    }
    function applyRange() {
        changes.setRange(scope.currentIndex, from.text, to.text)
    }
    Design.DialogSelect {
        id: scope
        Layout.fillWidth: true
        model: ["本地变更", "版本与工作区", "两个版本"]
        translateOptions: true
        Accessible.name: Design.UiText.get("比较范围")
        onActivated: root.applyRange()
    }
    RowLayout {
        visible: scope.currentIndex !== GitChanges.Local
        Layout.fillWidth: true
        Design.DialogInput {
            id: from
            Layout.fillWidth: true
            Layout.minimumWidth: 36
            text: "HEAD"
            placeholderText: Design.UiText.get("起始版本")
            Accessible.name: Design.UiText.get("起始版本")
            onAccepted: root.applyRange()
        }
        Design.DialogInput {
            id: to
            visible: scope.currentIndex === GitChanges.Revisions
            Layout.fillWidth: true
            Layout.minimumWidth: 36
            text: "HEAD"
            placeholderText: Design.UiText.get("目标版本")
            Accessible.name: Design.UiText.get("目标版本")
            onAccepted: root.applyRange()
        }
        Design.DialogAction { text: Design.UiText.get("比较"); onClicked: root.applyRange() }
    }
}
