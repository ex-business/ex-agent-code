pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import ExAgent.App as Design

// 所有菜单复用一套尺寸、避边和键盘策略；操作语义由调用方提供。
Menu {
    id: menu
    popupType: Popup.Item
    padding: 6
    margins: 10
    overlap: 0
    implicitWidth: 252
    font.pixelSize: 13
    background: Rectangle {
        radius: 12
        color: Design.Theme.dark ? "#292929" : "#ffffff"
        border.color: Design.Theme.dark ? "#464646" : "#e5e5e5"
        RectangularShadow { anchors.fill: parent; z: -1; radius: 12; blur: 24; offset: Qt.vector2d(0, 6); color: "#22000000" }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Design.Theme.pressDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Design.Theme.pressDuration } }
    delegate: Design.ContextMenuItem { }
}
