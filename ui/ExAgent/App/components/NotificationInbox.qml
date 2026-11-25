pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: panel
    required property NotificationCenter center
    required property bool writable
    signal visit(var entry)
    property bool unavailable: false
    property bool settingsPage: false
    function begin() { unavailable=false; settingsPage=false; open() }
    parent: Overlay.overlay; anchors.centerIn: Overlay.overlay; modal: true
    title: settingsPage ? "通知设置" : "通知"
    sizeClass: Design.WorkbenchDialog.Wide
    height: Math.min(420,parent.height-32)
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        RowLayout {
            Design.DialogAction { text: Design.UiText.get("收件箱") + " (" + panel.center.unread + ")"; primary: !panel.settingsPage; onClicked: panel.settingsPage=false }
            Design.DialogAction { text: Design.UiText.get("通知设置"); primary: panel.settingsPage; onClicked: panel.settingsPage=true }
        }
        Label { visible: panel.unavailable; text: Design.UiText.get("通知目标当前不可用"); color: Design.Theme.warning }
        ListView {
            id: list; visible: !panel.settingsPage; Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true; model: panel.center.entries
            ScrollBar.vertical: ScrollBar {}
            Label { anchors.centerIn: parent; visible: !list.count; text: Design.UiText.get("暂无通知"); color: Design.Theme.secondary }
            delegate: RowLayout {
                id: row
                required property var modelData
                width: list.width; spacing: 4
                ItemDelegate {
                    id: noticeAction
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Accessible.name: row.modelData.title
                    background: Rectangle { radius: 6; color: noticeAction.hovered || noticeAction.visualFocus ? Design.Theme.hover : "transparent" }
                    contentItem: ColumnLayout {
                        Label { text: row.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true; font.bold: !row.modelData.read }
                        Label { text: Design.UiText.get(["","等待处理","已完成","执行失败","已停止","结果待核对"][row.modelData.category] || "活动") + "  ·  " + Qt.formatDateTime(new Date(row.modelData.time),"MM-dd hh:mm"); font.pixelSize: 11; color: Design.Theme.secondary }
                    }
                    onClicked: panel.visit(row.modelData)
                }
                Design.IconButton {
                    iconName: row.modelData.read ? "bell" : "check"; enabled: panel.writable
                    tooltip: Design.UiText.get(row.modelData.read ? "标为未读" : "标为已读")
                    onClicked: panel.center.mark(row.modelData.id,!row.modelData.read)
                }
            }
        }
        ColumnLayout {
            visible: panel.settingsPage; Layout.fillWidth: true; Layout.fillHeight: true
            Repeater {
                model: [{key:"attention",title:"等待人工处理"}, {key:"outcomes",title:"运行结束"},
                    {key:"badge",title:"应用未读角标"}, {key:"desktop",title:"系统通知"}, {key:"sound",title:"系统通知声音"}]
                delegate: RowLayout {
                    id: setting
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: Design.UiText.get(setting.modelData.title); Layout.fillWidth: true }
                    Design.DialogAction {
                        enabled: panel.writable && (setting.modelData.key !== "desktop" || panel.center.nativeAvailable) &&
                            (setting.modelData.key !== "sound" || panel.center.desktop && panel.center.nativeAvailable)
                        text: Design.UiText.get(panel.center[setting.modelData.key] ? "已开启" : "已关闭")
                        Accessible.name: Design.UiText.get(setting.modelData.title)+" "+text
                        onClicked: panel.center[setting.modelData.key]=!panel.center[setting.modelData.key]
                    }
                }
            }
            Label { Layout.fillWidth: true; visible: !!text; wrapMode: Text.Wrap; color: Design.Theme.warning
                text: Design.UiText.get(panel.center.deliveryError) }
            Item { Layout.fillHeight: true }
        }
    }
}
