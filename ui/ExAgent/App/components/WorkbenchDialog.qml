pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

Dialog {
    id: panel
    property bool darkSurface: Design.Theme.dark
    padding: 20
    topPadding: 2
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
    background: Rectangle { radius: 12; color: panel.palette.window; border.color: panel.palette.mid }
    Overlay.modal: Rectangle { color: panel.darkSurface ? "#66080b10" : "#400d1119" }
    header: Label {
        text: panel.title; textFormat: Text.PlainText
        leftPadding: 20; rightPadding: 20; topPadding: 18; bottomPadding: 15
        font.pixelSize: 16; font.weight: Font.DemiBold; color: panel.palette.windowText
    }
    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 8; leftPadding: 20; rightPadding: 20; topPadding: 0; bottomPadding: 16
        background: Item {}
        delegate: DialogAction {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
    }
}
