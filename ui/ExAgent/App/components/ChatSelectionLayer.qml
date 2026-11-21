pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat

// 一次拖动由起点控件持有鼠标，终点通过注册的可见文本命中，不依赖起点继续存活。
Item {
    id: root
    required property ListView view
    required property MessageListModel model
    signal olderPageRequested()
    readonly property alias document: document
    property var parts: []
    property bool dragging: false
    property bool origin: false
    property var originPart: null
    property string anchorId: ""
    property int anchorBlock: 0
    property int anchorStart: 0
    property int anchorEnd: 0
    property int unit: 0
    property point pointer: Qt.point(0, 0)
    ChatSelectionDocument {
        id: document
        model: root.model
        onSelectionChanged: {
            root.parts.forEach(part => root.project(part))
            SelectionCoordinator.changed(document)
        }
        onModelChanged: root.finish()
    }
    function registerPart(part) {
        if (!parts.includes(part)) parts = parts.concat([part])
        project(part)
    }
    function unregisterPart(part) {
        parts = parts.filter(entry => entry !== part)
        if (originPart === part) originPart = null
    }
    function project(part) {
        if (!part || !part.target || part.messageId === "") return
        const range = document.range(part.messageId, part.blockIndex, part.textLength)
        part.projectRange(range.start, range.end)
    }
    function begin(part, start, end, selectionUnit, extendExisting) {
        view.cancelFlick()
        view.followingEnd = false
        origin = true
        originPart = part
        if (extendExisting && document.selectionLength > 0) {
            const anchor = document.anchorPosition()
            anchorId = anchor.id; anchorBlock = anchor.block
            anchorStart = anchor.offset; anchorEnd = anchor.offset
            unit = 0
            SelectionCoordinator.activate(document)
            document.select(anchorId, anchorBlock, anchorStart, part.messageId, part.blockIndex, end)
            return
        }
        anchorId = part.messageId
        anchorBlock = part.blockIndex
        anchorStart = start; anchorEnd = end
        unit = selectionUnit
        SelectionCoordinator.activate(document)
        document.select(anchorId, anchorBlock, start, anchorId, anchorBlock, end)
    }
    function visibleBounds(target) {
        const top = target.mapToItem(view, 0, 0)
        let left = Math.max(0, top.x), upper = Math.max(0, top.y)
        let right = Math.min(view.width, top.x + target.width), lower = Math.min(view.height, top.y + target.height)
        // 内部文件列表等裁剪视口之外的缓存行，不得参与鼠标命中。
        for (let item = target.parent, depth = 0; item && item !== view && depth < 64; item = item.parent, ++depth) {
            if (!item.clip) continue
            const p = item.mapToItem(view, 0, 0)
            left = Math.max(left, p.x); upper = Math.max(upper, p.y)
            right = Math.min(right, p.x + item.width); lower = Math.min(lower, p.y + item.height)
        }
        return {left: left, top: upper, right: right, bottom: lower}
    }
    function extendAt(scenePoint) {
        if (!origin) return
        dragging = true
        pointer = scenePoint
        const viewportPoint = view.mapFromItem(null, scenePoint.x, scenePoint.y)
        const bounded = view.mapToItem(null, Math.max(0, Math.min(view.width, viewportPoint.x)), Math.max(1, Math.min(view.height - 1, viewportPoint.y)))
        let closest = null, localPoint = null, distance = Infinity
        for (const part of parts) {
            if (!part.target || !part.target.visible || part.messageId === "") continue
            const bounds = visibleBounds(part.target)
            if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) continue
            const at = view.mapFromItem(null, bounded.x, bounded.y)
            const dx = Math.max(bounds.left - at.x, 0, at.x - bounds.right)
            const dy = Math.max(bounds.top - at.y, 0, at.y - bounds.bottom)
            const p = part.target.mapFromItem(view, Math.max(bounds.left, Math.min(bounds.right, at.x)),
                                            Math.max(bounds.top, Math.min(bounds.bottom, at.y)))
            const score = dy * 10000 + dx
            if (score < distance) { closest = part; localPoint = p; distance = score }
        }
        if (!closest) return
        let position = closest.positionFor(localPoint.x, localPoint.y)
        const backwards = document.orderOf(closest.messageId) < document.orderOf(anchorId)
            || closest.messageId === anchorId && (closest.blockIndex < anchorBlock || closest.blockIndex === anchorBlock && position < anchorStart)
        if (unit > 0) {
            const span = closest.rangeFor(position, unit)
            position = backwards ? span.start : span.end
        }
        document.select(anchorId, anchorBlock, backwards ? anchorEnd : anchorStart,
                        closest.messageId, closest.blockIndex, position)
    }
    function innerOwnsPointer() {
        // 委托可能已复用给另一条消息，不能把新内容误认为原拖选起点。
        if (!originPart || originPart.messageId !== anchorId || originPart.blockIndex !== anchorBlock) return false
        const inner = originPart.scrollView
        if (!inner) return false
        const p = inner.mapFromItem(null, pointer.x, pointer.y)
        // 代码内部仍有内容时先在代码内扩选，越出交接带或到边界后交给聊天。
        if (p.y < -24 || p.y > inner.height + 24) return false
        const low = inner.originY - inner.topMargin
        const high = Math.max(low, inner.originY + inner.contentHeight - inner.height + inner.bottomMargin)
        return p.y < 18 && inner.contentY > low + 0.5 || p.y > inner.height - 18 && inner.contentY < high - 0.5
    }
    function finish() { dragging = false; origin = false; originPart = null }
    function copySelection() { SelectionCoordinator.activate(document); SelectionCoordinator.copy() }
    function selectLoaded() { SelectionCoordinator.activate(document); document.selectLoaded() }
    ChatSelectionTracker {
        tracking: root.origin
        onDragMoved: (point) => root.extendAt(point)
        onDragReleased: (point) => { root.extendAt(point); root.finish() }
        onDragCancelled: root.finish()
    }
    Connections {
        target: root.model
        function onModelAboutToBeReset() { root.finish() }
    }
    SelectionScrollClock {
        readonly property point localPointer: root.view.mapFromItem(null, root.pointer.x, root.pointer.y)
        running: root.dragging && !root.innerOwnsPointer() && (localPointer.y < 24 || localPointer.y > root.view.height - 24)
        onTick: (seconds) => {
            const p = root.view.mapFromItem(null, root.pointer.x, root.pointer.y)
            const dy = p.y < 24 ? p.y - 24 : p.y > root.view.height - 24 ? p.y - root.view.height + 24 : 0
            if (dy === 0) return
            root.view.cancelFlick()
            const low = root.view.originY - root.view.topMargin
            const high = Math.max(low, root.view.originY + root.view.contentHeight - root.view.height + root.view.bottomMargin)
            root.view.contentY = Math.max(low, Math.min(high, root.view.contentY + Math.max(-2250, Math.min(2250, dy * 28.125)) * seconds))
            if (dy < 0 && root.view.contentY - root.view.originY < 100) root.olderPageRequested()
            root.extendAt(root.pointer)
        }
    }
}
