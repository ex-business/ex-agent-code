pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design
Item {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    signal openWindow(string component, var properties, url resource, string title)
    Design.WorkspaceImageViewer {
        anchors.fill: parent; imageInfo: root.workspace.imageInfo; controller: root.controller
        onDetachRequested: root.openWindow("WorkspaceImageViewer.qml",{imageInfo:root.workspace.imageInfo,allowDetach:false},root.workspace.imageInfo.url,root.workspace.imageInfo.name)
    }
}
