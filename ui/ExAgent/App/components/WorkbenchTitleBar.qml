pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Rectangle {
    id: root
    required property ApplicationWindow windowHandle
    required property WorkspaceController workspace
    property string sessionTitle: "新聊天"
    property real navigationWidth: 375
    property real filesWidth: 0
    property bool arranging: false
    signal arrangeRequested()
    signal resetRequested()
    signal moveRequested()
    signal sidebarRequested()
    signal filesRequested()
    signal menuRequested()
    implicitHeight: 38
    color: Design.Theme.sidebar
    MouseArea {
        anchors.fill: parent
        onPressed: root.windowHandle.startSystemMove()
        onDoubleClicked: root.windowHandle.visibility === Window.Maximized ? root.windowHandle.showNormal() : root.windowHandle.showMaximized()
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.preferredWidth: root.navigationWidth
            Layout.minimumWidth: Qt.platform.os === "osx" ? 182 : 110
            Layout.maximumWidth: Math.max(Layout.minimumWidth, root.navigationWidth)
            Layout.fillWidth: false
            spacing: 5
            Item { Layout.preferredWidth: Qt.platform.os === "osx" ? 82 : 8 }
            Design.IconButton { iconName: "back"; tooltip: "返回，暂无上一项"; enabled: false; opacity: 0.4; implicitWidth: 26 }
            Design.IconButton { iconName: "right"; tooltip: "前进，暂无下一项"; enabled: false; opacity: 0.4; implicitWidth: 26 }
            Design.IconButton { iconName: "sidebar"; tooltip: Design.UiText.get("显示或收起导航"); onClicked: root.sidebarRequested(); implicitWidth: 26 }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 8
            spacing: 6
            Design.Icon { name: "folder"; implicitWidth: 14; implicitHeight: 14 }
            Text { text: root.arranging ? Design.UiText.get("拖到边缘拆分，拖到中央合并") : root.sessionTitle; Layout.fillWidth: true; elide: Text.ElideRight; color: Design.Theme.text; font.pixelSize: 12 }
            Design.IconButton { iconName: "grid"; iconColor: root.arranging ? Design.Theme.accent : Design.Theme.secondary; tooltip: Design.UiText.get(root.arranging ? "完成面板布局" : "调整面板布局"); onClicked: root.arrangeRequested(); implicitWidth: 26 }
            Design.IconButton { visible: root.arranging; iconName: "expand"; tooltip: Design.UiText.get("移动面板"); onClicked: root.moveRequested(); implicitWidth: 26 }
            Design.IconButton { visible: root.arranging; iconName: "refresh"; tooltip: Design.UiText.get("恢复默认布局"); onClicked: root.resetRequested(); implicitWidth: 26 }
            Design.IconButton { iconName: "sidebar"; tooltip: "显示或收起文件面板"; onClicked: root.filesRequested(); implicitWidth: 26 }
            Design.IconButton { iconName: "dots"; tooltip: "聊天操作"; onClicked: root.menuRequested(); implicitWidth: 26 }
        }
        Design.WorkspaceTabs { visible: root.filesWidth > 0; Layout.preferredWidth: root.filesWidth; Layout.maximumWidth: root.filesWidth; Layout.fillWidth: false; Layout.fillHeight: true; workspace: root.workspace }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Design.Theme.border }
}
