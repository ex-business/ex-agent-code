pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

// 确认动作由业务方完成后再关闭，失败时保留弹窗、错误和输入上下文。
WorkbenchDialog {
    id: panel
    property string message: ""
    property string detail: ""
    property string contextText: ""
    property bool detailIsError: false
    property string primaryText: "确定"
    property string secondaryText: ""
    property bool destructive: false
    property bool secondaryDestructive: false
    signal primaryRequested()
    signal secondaryRequested()
    modal: true
    anchors.centerIn: Overlay.overlay
    onOpened: Qt.callLater(() => { if (panel.visible) actions.focusDefault() })
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            visible: !!panel.message
            text: Design.UiText.get(panel.message); textFormat: Text.PlainText
            opacity: 0.8; font.pixelSize: 13; wrapMode: Text.Wrap
            Layout.fillWidth: true; Layout.minimumWidth: 0
        }
        Label {
            visible: !!panel.contextText
            text: panel.contextText; textFormat: Text.PlainText
            font.family: Design.Theme.monoFont; font.pixelSize: 12; opacity: 0.8; wrapMode: Text.Wrap
            Layout.fillWidth: true; Layout.minimumWidth: 0
        }
        Control {
            visible: !!panel.detail
            padding: 12
            Layout.fillWidth: true; Layout.minimumWidth: 0
            background: Rectangle {
                radius: 8
                color: panel.detailIsError ? panel.warningSurface : panel.palette.base
                border.color: panel.detailIsError ? panel.warningBorder : panel.palette.mid
            }
            contentItem: Label {
                text: panel.detail.split("\n").map(line => Design.UiText.get(line)).join("\n")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 12
            }
        }
    }
    footer: WorkbenchDialogActions {
        id: actions
        primaryText: panel.primaryText; secondaryText: panel.secondaryText; destructive: panel.destructive; secondaryDestructive: panel.secondaryDestructive
        onPrimaryRequested: panel.primaryRequested()
        onSecondaryRequested: panel.secondaryRequested()
        onCancelled: panel.reject()
    }
}
