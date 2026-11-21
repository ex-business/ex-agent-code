pragma Singleton
import QtQuick

QtObject {
    property bool dark: false
    property color background: dark ? "#191919" : "#ffffff"
    property color sidebar: dark ? "#222222" : "#fafafa"
    property color surface: dark ? "#2f2f2f" : "#f1f1f1"
    property color hover: dark ? "#343434" : "#e9e9e9"
    property color border: dark ? "#383838" : "#eeeeef"
    property color text: dark ? "#ececec" : "#202020"
    property color secondary: dark ? "#a0a0a0" : "#696969"
    property color muted: dark ? "#777777" : "#8b8b8b"
    property color code: dark ? "#141414" : "#f6f6f6"
    property color selected: dark ? "#3a3a3a" : "#e5e5e5"
    property color accent: dark ? "#ececec" : "#242424"
    property color onAccent: dark ? "#202020" : "#ffffff"
    property color warning: dark ? "#e5b07a" : "#986020"
    readonly property string font: Qt.platform.os === "osx" ? ".AppleSystemUIFont" : "Sans Serif"
    readonly property string monoFont: Qt.platform.os === "osx" ? "Menlo" : "monospace"
}
