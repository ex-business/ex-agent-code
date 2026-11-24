pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import ExAgent.App as Design

// 弹窗实例只选择尺寸档位和业务内容，视觉参数统一从 Theme 获取。
Dialog {
    id: panel
    property bool darkSurface: Design.Theme.dark
    readonly property color warningSurface: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelWarningSurface : darkSurface ? "#352a21" : "#fff6e8"
    readonly property color warningBorder: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelWarningBorder : darkSurface ? "#725334" : "#ead3af"
    enum SizeClass { Compact=0, Normal=1, Wide=2, Tool=3 }
    property int sizeClass: WorkbenchDialog.Normal
    width: Math.min(Design.Theme.dialogWidths[sizeClass] || Design.Theme.dialogWidths[1], Overlay.overlay ? Math.max(240,Overlay.overlay.width-2*Design.Theme.dialogScreenMargin) : 480)
    // 长表单按可用高度滚动，标题和动作栏始终留在窗口内。
    readonly property real maximumBodyHeight: Math.max(80, (Overlay.overlay ? Overlay.overlay.height : 800) - 2*Design.Theme.dialogScreenMargin - (header ? header.implicitHeight : 0) - (footer && footer.visible ? footer.implicitHeight : 0) - topPadding - bottomPadding)
    padding: Design.Theme.dialogPadding
    topPadding: 0
    bottomPadding: footer && footer.visible ? 0 : Design.Theme.dialogPadding
    spacing: 0
    font.family: Design.Theme.font
    font.pixelSize: 13
    palette.window: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelWindow : darkSurface ? "#20262e" : "#ffffff"
    palette.windowText: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelWindowText : darkSurface ? "#e3e8ef" : "#24272c"
    palette.text: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelText : darkSurface ? "#e3e8ef" : "#24272c"
    palette.buttonText: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelButtonText : darkSurface ? "#e3e8ef" : "#24272c"
    palette.base: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelBase : darkSurface ? "#171c23" : "#fafafb"
    palette.button: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelButton : darkSurface ? "#303843" : "#f1f2f4"
    palette.mid: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelMid : darkSurface ? "#424c59" : "#d9dce1"
    palette.placeholderText: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelPlaceholderText : darkSurface ? "#a2abb8" : "#737a84"
    palette.highlight: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelHighlight : darkSurface ? "#88a9ca" : "#526d89"
    palette.highlightedText: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelHighlightedText : darkSurface ? "#14202d" : "#ffffff"
    background: Rectangle {
        radius: Design.Theme.dialogRadius; color: panel.palette.window; border.color: panel.palette.mid
        RectangularShadow { anchors.fill: parent; z: -1; radius: Design.Theme.dialogRadius; blur: 32; offset: Qt.vector2d(0,8); color: panel.darkSurface ? "#55000000" : "#22000000" }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Design.Theme.pressDuration; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Design.Theme.pressDuration } }
    Overlay.modal: Rectangle { color: panel.darkSurface ? "#66080b10" : "#260d1119" }
    header: Label {
        text: Design.UiText.get(panel.title); textFormat: Text.PlainText
        leftPadding: Design.Theme.dialogPadding; rightPadding: Design.Theme.dialogPadding; topPadding: Design.Theme.dialogPadding; bottomPadding: Design.Theme.dialogHeaderGap
        font.pixelSize: Design.Theme.dialogTitleSize; font.weight: Font.DemiBold; color: panel.palette.windowText
    }
    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: Design.Theme.dialogButtonGap; leftPadding: Design.Theme.dialogPadding; rightPadding: Design.Theme.dialogPadding; topPadding: Design.Theme.dialogFooterPadding; bottomPadding: Design.Theme.dialogFooterPadding
        background: Item {}
        delegate: DialogAction {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
            quiet: DialogButtonBox.buttonRole === DialogButtonBox.RejectRole
        }
    }
}
