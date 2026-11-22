pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    implicitHeight: 226
    Design.MediaSession { id: playback; block: root.block; controller: root.controller }
    Design.MediaPreview { anchors.fill: parent; session: playback }
}
