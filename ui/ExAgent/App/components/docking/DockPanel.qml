pragma ComponentBehavior: Bound
import QtQuick

Item {
    required property DockSurface host
    required property string panelId
    readonly property var bounds: host.positions.panels[panelId]
    visible: !!bounds
    // 标签切到后台时保留最后的视口尺寸，避免聊天和编辑器按零宽重新排版。
    onBoundsChanged: {
        if (!bounds) return
        x = bounds.x; y = bounds.y; width = bounds.w; height = bounds.h
    }
    clip: true
}
