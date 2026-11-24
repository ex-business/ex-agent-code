pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design

// 所有自定义提交动作复用此按钮栏；业务方成功后才关闭弹窗。
Control {
    id: bar
    property string primaryText: "确定"
    property string secondaryText: ""
    property bool destructive: false
    property bool secondaryDestructive: false
    property bool primaryEnabled: true
    signal primaryRequested()
    signal secondaryRequested()
    signal cancelled()
    function focusDefault() {
        if (destructive) cancelAction.forceActiveFocus(Qt.PopupFocusReason)
        else primaryAction.forceActiveFocus(Qt.PopupFocusReason)
    }
    leftPadding: Design.Theme.dialogPadding; rightPadding: Design.Theme.dialogPadding
    topPadding: Design.Theme.dialogFooterPadding; bottomPadding: Design.Theme.dialogFooterPadding
    background: Item {}
    contentItem: RowLayout {
        spacing: Design.Theme.dialogButtonGap
        Item { Layout.fillWidth: true }
        DialogAction { id: cancelAction; text: Design.UiText.get("取消"); quiet: true; focus: bar.destructive; onClicked: bar.cancelled() }
        DialogAction { text: Design.UiText.get(bar.secondaryText); visible: !!bar.secondaryText; destructive: bar.secondaryDestructive; onClicked: bar.secondaryRequested() }
        DialogAction { id: primaryAction; text: Design.UiText.get(bar.primaryText); primary: true; destructive: bar.destructive; enabled: bar.primaryEnabled; focus: !bar.destructive; onClicked: bar.primaryRequested() }
    }
}
