pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

Item {
    id: chrome
    property string title: "图片"
    property string subtitle: ""
    property real zoomScale: 1
    property bool canSave: false
    signal closeRequested()
    signal saveRequested()
    signal zoomRequested(real factor)
    signal fitRequested()
    signal originalRequested()
    RowLayout {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 18; spacing: 10
        ColumnLayout {
            Layout.fillWidth: true; spacing: 3
            Text { Layout.fillWidth: true; text: chrome.title; elide: Text.ElideMiddle; color: "#f0f0f0"; font.pixelSize: 13 }
            Text { text: chrome.subtitle; color: "#a5a5a5"; font.pixelSize: 11 }
        }
        Design.MediaToolButton { iconName: "download"; tooltip: "保存原始图片"; enabled: chrome.canSave; onClicked: chrome.saveRequested() }
        Design.MediaToolButton { iconName: "close"; tooltip: "关闭图片查看器"; onClicked: chrome.closeRequested() }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 15; anchors.horizontalCenter: parent.horizontalCenter
        width: zoomRow.implicitWidth + 12; height: 44; radius: 22; color: "#ee303030"; border.color: "#555555"
        RowLayout {
            id: zoomRow
            anchors.centerIn: parent; spacing: 3
            Design.MediaToolButton { iconName: "minus"; tooltip: "缩小图片"; onClicked: chrome.zoomRequested(1 / 1.25) }
            Text { text: Math.round(chrome.zoomScale * 100) + "%"; color: "#ffffff"; font.pixelSize: 12; Layout.minimumWidth: 48; horizontalAlignment: Text.AlignHCenter }
            Design.MediaToolButton { iconName: "plus"; tooltip: "放大图片"; onClicked: chrome.zoomRequested(1.25) }
            Rectangle { implicitWidth: 1; implicitHeight: 18; color: "#666666"; Layout.leftMargin: 5; Layout.rightMargin: 5 }
            Design.MediaToolButton { iconName: "fit"; tooltip: "适应窗口"; onClicked: chrome.fitRequested() }
            Design.MediaToolButton { iconName: "original"; tooltip: "原始尺寸"; onClicked: chrome.originalRequested() }
        }
    }
}
