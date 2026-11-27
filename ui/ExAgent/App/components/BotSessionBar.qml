pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

RowLayout {
    id: root
    required property ChatController controller
    signal manageRequested()
    readonly property var bot: controller.bots ? controller.bots.current : ({})
    visible: !!bot.id
    spacing: 8
    Label { Layout.leftMargin: 14; Layout.fillWidth: true; text: (root.bot.name || "") + " · " + Design.UiText.get(["准备中", "可接收输入", "已暂停", "不可用"][root.bot.state] || ""); textFormat: Text.PlainText; elide: Text.ElideRight; color: Design.Theme.secondary }
    DialogAction { Layout.rightMargin: 14; text: Design.UiText.get("管理 Bot"); quiet: true; onClicked: root.manageRequested() }
}
