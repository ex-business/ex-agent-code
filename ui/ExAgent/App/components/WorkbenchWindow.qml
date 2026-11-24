pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

ApplicationWindow {
    // 子控件继承工作台配色，主题切换时输入框与菜单同步刷新。
    palette.window: Design.Theme.background
    palette.windowText: Design.Theme.text
    palette.text: Design.Theme.text
    palette.buttonText: Design.Theme.text
    palette.base: Design.Theme.tokens.panelBase
    palette.button: Design.Theme.tokens.panelButton
    palette.mid: Design.Theme.tokens.panelMid
    palette.placeholderText: Design.Theme.tokens.panelPlaceholderText
    palette.highlight: Design.Theme.tokens.panelHighlight
    palette.highlightedText: Design.Theme.tokens.panelHighlightedText
}
