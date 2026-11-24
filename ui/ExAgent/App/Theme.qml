pragma Singleton
import QtQuick
import ExAgent.Chat

QtObject {
    readonly property bool dark: AppearanceRegistry.dark
    readonly property var tokens: AppearanceRegistry.colors
    function toggle() { AppearanceRegistry.theme = dark ? "light" : "dark" }
    readonly property var dialogWidths: [440, 480, 720, 960]
    readonly property int dialogPadding: 24
    readonly property int dialogRadius: 12
    readonly property int dialogTitleSize: 17
    readonly property int dialogButtonHeight: 34
    readonly property int dialogButtonGap: 8
    readonly property int dialogHeaderGap: 12
    readonly property int dialogFooterPadding: 20
    readonly property int dialogScreenMargin: 20
    property bool motionEnabled: true
    readonly property int pressDuration: motionEnabled ? 90 : 0
    readonly property int disclosureDuration: motionEnabled ? 160 : 0
    readonly property color background: tokens.background
    readonly property color sidebar: tokens.sidebar
    readonly property color surface: tokens.surface
    readonly property color hover: tokens.hover
    readonly property color border: tokens.border
    readonly property color text: tokens.text
    readonly property color secondary: tokens.secondary
    readonly property color muted: tokens.muted
    readonly property color code: tokens.code
    readonly property color selected: tokens.selected
    readonly property color accent: tokens.accent
    readonly property color onAccent: tokens.onAccent
    readonly property color danger: tokens.danger
    readonly property color warning: tokens.warning
    readonly property string font: Qt.platform.os === "osx" ? ".AppleSystemUIFont" : "Sans Serif"
    readonly property string monoFont: Qt.platform.os === "osx" ? "Menlo" : "monospace"
}
