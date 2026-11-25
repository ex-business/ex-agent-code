pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: dialog
    required property var titles
    signal moveRequested(string sourceId, string targetId, int side)
    readonly property var ids: Object.keys(titles)
    readonly property var destinations: ids.filter(id => id !== ids[source.currentIndex])
    function begin() { source.currentIndex = 0; target.currentIndex = 0; position.currentIndex = 0; open() }
    title: Design.UiText.get("移动面板")
    modal: true; parent: Overlay.overlay; anchors.centerIn: parent
    contentItem: ColumnLayout {
        spacing: 8
        Label { text: Design.UiText.get("移动面板") }
        Design.DialogSelect {
            id: source; Layout.fillWidth: true; model: dialog.ids.map(id => dialog.titles[id])
            translateOptions: true
            onActivated: target.currentIndex = 0
        }
        Label { text: Design.UiText.get("目标面板") }
        Design.DialogSelect {
            id: target; Layout.fillWidth: true; model: dialog.destinations.map(id => dialog.titles[id])
            translateOptions: true
        }
        Design.DialogSelect {
            id: position; Layout.fillWidth: true; translateOptions: true
            Accessible.name: Design.UiText.get("放置位置")
            model: ["合并为标签组","放在左侧","放在右侧","放在上方","放在下方"]
        }
    }
    footer: Design.WorkbenchDialogActions {
        primaryText: Design.UiText.get("移动")
        primaryEnabled: target.currentIndex >= 0 && !!dialog.destinations[target.currentIndex]
        onPrimaryRequested: {
            dialog.moveRequested(dialog.ids[source.currentIndex],dialog.destinations[target.currentIndex],position.currentIndex)
            dialog.close()
        }
        onCancelled: dialog.reject()
    }
}
