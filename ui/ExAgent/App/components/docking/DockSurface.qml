pragma ComponentBehavior: Bound
import QtQuick
import QtCore as Core
import ExAgent.App as Design
import "PanelTree.js" as Tree

Item {
    id: layoutHost
    required property var titles
    property var minimumSizes: ({})
    required property var makeDefault
    required property var initialHidden
    property var optionalTabs: ({})
    property var tree: null
    property var hidden: []
    property var suppressed: []
    property bool arranging: false
    property bool customized: false
    property string scopeKey: ""
    property string dragging: ""
    property var destination: null
    readonly property var positions: Tree.geometry(tree, hidden.concat(arranging ? [] : suppressed),width,height,arranging,minimumSizes)
    PanelMoveDialog {
        id: moveDialog
        titles: layoutHost.titles
        onMoveRequested: (sourceId,targetId,side) => layoutHost.movePanel(sourceId,targetId,side)
    }
    function chooseMove() { moveDialog.begin() }
    Core.Settings { id: prefs; category: "WorkspacePanels" }

    function store() {
        if (!tree || !scopeKey) return
        prefs.setValue(scopeKey,JSON.stringify({version:1,tree,hidden,customized}))
        prefs.sync()
    }
    function restore(key: string) {
        const next = "layout-" + Qt.md5(key)
        if (next === scopeKey) return
        store()
        scopeKey = next
        const ids = Object.keys(titles)
        let saved = null
        try { saved = JSON.parse(prefs.value(next,"null")) } catch (e) {}
        // 新插件作为隐藏页签加入旧布局，保留用户拆分的比例、顺序和选中页。
        if (saved && saved.version === 1 && Array.isArray(saved.hidden)) {
            const additions = Object.keys(optionalTabs)
            if (additions.length && Tree.validate(saved.tree,ids.filter(id=>!additions.includes(id)))) {
                for (const id of additions) {
                    if (Tree.appendTab(saved.tree,optionalTabs[id],id)) saved.hidden.push(id)
                }
            }
        }
        const valid = saved && saved.version === 1 && Tree.validate(saved.tree,ids)
                && Array.isArray(saved.hidden) && saved.hidden.every(id => ids.includes(id)) && saved.hidden.length < ids.length
        dragging = ""; destination = null; arranging = false
        customized = valid && saved.customized === true
        hidden = valid ? saved.hidden : initialHidden.slice()
        tree = valid ? saved.tree : makeDefault(false)
    }
    function resetLayout() {
        dragging = ""; destination = null
        customized = false; hidden = Object.keys(optionalTabs); tree = makeDefault(true)
        store()
    }
    function showPanel(id: string) {
        if (!tree) return
        hidden = hidden.filter(key => key !== id)
        const next = JSON.parse(JSON.stringify(tree))
        Tree.activate(next,id)
        tree = next
        store()
    }
    function togglePanel(id: string) {
        if (!positions.panels[id]) showPanel(id)
        else if (!hidden.includes(id)) { hidden = hidden.concat([id]); store() }
    }
    function movePanel(source: string,target: string,side: int) {
        if (!tree || source === target) return
        tree = Tree.place(tree,source,target,side)
        customized = true
        hidden = hidden.filter(id => id !== source && id !== target)
        store()
    }
    function hit(px: real,py: real,source: string): var {
        for (const group of positions.groups) {
            const x = px-group.x, y = py-group.y
            if (x<0 || y<0 || x>=group.w || y>=group.h) continue
            if (y < group.bar) {
                const tabWidth = group.w/group.tabs.length
                const index = Math.min(group.tabs.length-1,Math.floor(x/tabWidth))
                const target = group.tabs[index]
                return target === source ? null : {target,side:x%tabWidth < tabWidth/2 ? 5 : 0,
                    x:group.x+index*tabWidth,y:group.y,w:tabWidth,h:group.bar}
            }
            const rx = x/group.w, ry = (y-group.bar)/Math.max(1,group.h-group.bar)
            const edge = Math.min(rx,1-rx,ry,1-ry)
            const side = edge > 0.24 ? 0 : edge === rx ? 1 : edge === 1-rx ? 2 : edge === ry ? 3 : 4
            const target = group.active !== source ? group.active : group.tabs.find(id => id !== source)
            if (!target) return null
            return {target,side,x:group.x+(side===2 ? group.w/2 : 0),y:group.y+(side===4 ? group.h/2 : 0),
                w:side===1 || side===2 ? group.w/2 : group.w,h:side===3 || side===4 ? group.h/2 : group.h}
        }
        return null
    }
    Repeater {
        model: layoutHost.positions.groups.length
        delegate: Row {
            id: header
            required property int index
            readonly property var group: layoutHost.positions.groups[index]
            x: group.x; y: group.y; width: group.w; height: group.bar
            visible: height > 0; z: 2
            Repeater {
                model: header.group.tabs.length
                delegate: Rectangle {
                    id: tab
                    required property int index
                    readonly property string key: header.group.tabs[index] || ""
                    width: header.width / header.group.tabs.length
                    height: header.height
                    color: header.group.active === key ? Design.Theme.selected : Design.Theme.sidebar
                    Text {
                        anchors.centerIn: parent; width: parent.width-8; horizontalAlignment: Text.AlignHCenter
                        text: Design.UiText.get(layoutHost.titles[tab.key] || ""); elide: Text.ElideRight
                        color: Design.Theme.text; font.pixelSize: 12
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        visible: header.group.tabs.length > 1 && header.group.active === tab.key
                        color: Design.Theme.accent
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent; hoverEnabled: true; preventStealing: true
                        activeFocusOnTab: true
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: header.group.active === tab.key
                        Accessible.name: Design.UiText.get(layoutHost.titles[tab.key] || "")
                        Accessible.onPressAction: layoutHost.showPanel(tab.key)
                        Keys.onSpacePressed: layoutHost.showPanel(tab.key)
                        Keys.onReturnPressed: layoutHost.showPanel(tab.key)
                        cursorShape: layoutHost.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        property point start
                        property string source: ""
                        onPressed: event => {
                            source = tab.key
                            start = mapToItem(layoutHost,event.x,event.y)
                            forceActiveFocus(Qt.MouseFocusReason)
                        }
                        onPositionChanged: event => {
                            if (!pressed) return
                            const p = mapToItem(layoutHost,event.x,event.y)
                            if (!layoutHost.dragging && Math.hypot(p.x-start.x,p.y-start.y)<mouse.drag.threshold) return
                            layoutHost.dragging = source
                            layoutHost.destination = layoutHost.hit(p.x,p.y,source)
                        }
                        onReleased: event => {
                            const sourceId = source, moved = !!layoutHost.dragging
                            const p = mapToItem(layoutHost,event.x,event.y)
                            const dst = moved ? layoutHost.hit(p.x,p.y,sourceId) : null
                            layoutHost.dragging = ""; layoutHost.destination = null
                            if (dst) layoutHost.movePanel(sourceId,dst.target,dst.side)
                            else if (!moved) layoutHost.showPanel(sourceId)
                        }
                        onCanceled: { layoutHost.dragging = ""; layoutHost.destination = null }
                    }
                }
            }
        }
    }
    Repeater {
        model: layoutHost.positions.handles.length
        delegate: Rectangle {
            id: divider
            required property int index
            readonly property var info: layoutHost.positions.handles[index]
            x: info.x; y: info.y; width: info.w; height: info.h; z: 3
            color: grip.pressed ? Design.Theme.selected : grip.containsMouse ? Design.Theme.hover : "transparent"
            Rectangle {
                anchors.centerIn: parent; width: divider.info.hor ? 1 : parent.width
                height: divider.info.hor ? parent.height : 1; color: Design.Theme.border
            }
            MouseArea {
                id: grip
                anchors.fill: parent; hoverEnabled: true; preventStealing: true
                cursorShape: divider.info.hor ? Qt.SplitHCursor : Qt.SplitVCursor
                Accessible.role: Accessible.Separator
                Accessible.name: Design.UiText.get("调整面板大小")
                property real offset
                onPressed: event => { offset = divider.info.hor ? event.x : event.y }
                onPositionChanged: event => {
                    if (!pressed) return
                    const info = divider.info, p = mapToItem(layoutHost,event.x,event.y)
                    const size = Math.max(info.low,Math.min(info.high,(info.hor?p.x:p.y)-info.origin-offset))
                    if (info.total > 0) layoutHost.tree = Tree.resize(layoutHost.tree,info.path,size/info.total)
                }
                onReleased: layoutHost.store()
            }
        }
    }
    Rectangle {
        z: 4; visible: !!layoutHost.destination
        x: layoutHost.destination?.x || 0; y: layoutHost.destination?.y || 0
        width: layoutHost.destination?.w || 0; height: layoutHost.destination?.h || 0
        color: Qt.alpha(Design.Theme.accent,0.13); border.color: Design.Theme.accent
    }
}
