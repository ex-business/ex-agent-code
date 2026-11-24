import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: 36
    leftPadding: 11; rightPadding: 11
    color: palette.text; placeholderTextColor: palette.placeholderText
    background: Rectangle {
        radius: 6; color: field.palette.base
        border.color: field.activeFocus ? field.palette.highlight : field.palette.mid
    }
}
