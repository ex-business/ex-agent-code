pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

ComboBox {
    id: select
    // 翻译只影响显示文本，保持模型索引和未保存的选择。
    property bool translateOptions: false
    implicitHeight: 36
    leftPadding: 11
    rightPadding: 32
    contentItem: TextField {
        padding: 0; background: null
        enabled: select.editable; readOnly: select.down
        selectByMouse: select.selectTextByMouse; validator: select.validator
        inputMethodHints: select.inputMethodHints
        text: select.editable ? select.editText : select.translateOptions ? Design.UiText.get(select.displayText) : select.displayText; font: select.font
        color: select.enabled ? select.palette.text : select.palette.placeholderText
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Design.Icon {
        name: "down"; color: select.palette.placeholderText
        width: 14; height: 14; x: select.width - width - 11; y: (select.height - height) / 2
    }
    background: Rectangle {
        radius: 6; color: select.palette.base
        border.color: select.visualFocus || select.popup.visible ? select.palette.highlight : select.palette.mid
    }
    popup: Popup {
        y: select.height + 4; width: select.width; padding: 4
        implicitHeight: Math.min(contentView.contentHeight + 8, 200)
        background: Rectangle { radius: 7; color: select.palette.window; border.color: select.palette.mid }
        contentItem: ListView {
            id: contentView
            clip: true; model: select.popup.visible ? select.delegateModel : null
            currentIndex: select.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
    delegate: ItemDelegate {
        required property int index
        width: select.width - 8; height: 30
        text: select.translateOptions ? Design.UiText.get(select.textAt(index)) : select.textAt(index); highlighted: select.highlightedIndex === index
        background: Rectangle { radius: 4; color: parent.highlighted ? select.palette.button : "transparent" }
    }
}
