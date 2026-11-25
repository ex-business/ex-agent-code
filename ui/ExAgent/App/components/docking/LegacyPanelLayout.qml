pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtCore as Core

Item {
    id: legacy
    visible: false
    readonly property bool sidebarShown: settings.value("sidebarShown",true)
    readonly property bool filesShown: settings.value("filesShown",true)
    property real navWidth: 0
    property real previewWidth: 0
    Core.Settings { id: settings }
    function load() {
        navWidth = Number(settings.value("navWidth",0))
        previewWidth = Number(settings.value("previewWidth",0))
        const state = settings.value("splitState")
        if (!state || (navWidth > 0 && previewWidth > 0)) return
        // 早期版本只保存 Qt 的二进制状态，通过原控件恢复，避免依赖私有格式。
        const split = reader.createObject(legacy) as SplitView
        if (!split) return
        if (split.restoreState(state)) {
            if (!(navWidth > 0)) navWidth = split.itemAt(0).SplitView.preferredWidth
            if (!(previewWidth > 0)) previewWidth = split.itemAt(2).SplitView.preferredWidth
        }
        split.destroy()
    }
    Component {
        id: reader
        SplitView {
            Item { SplitView.preferredWidth: 325 }
            Item { SplitView.fillWidth: true }
            Item { SplitView.preferredWidth: 600 }
        }
    }
}
