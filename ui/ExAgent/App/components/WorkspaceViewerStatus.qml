pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
ColumnLayout {
    id: root
    required property WorkspaceViewerData model
    visible: model.state!==WorkspaceViewerData.Ready
    spacing: 12
    Label { text: root.model.state===WorkspaceViewerData.Loading ? Design.UiText.get("正在加载…") : Design.UiText.get(root.model.content.error || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Design.Theme.secondary; Layout.fillWidth: true }
    Design.DialogAction { visible: root.model.state===WorkspaceViewerData.Failed; text: Design.UiText.get("重新载入"); onClicked: root.model.reload() }
}
