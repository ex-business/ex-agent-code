pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

Menu {
    popupType: Popup.Item
    padding: 5
    implicitWidth: 218
    font.pixelSize: 12
    background: Rectangle {
        radius: 9
        color: Design.Theme.sidebar
        border.color: Design.Theme.border
    }
}
