.pragma library

// 布局树只保存面板注册 ID。移动、合并和恢复都不持有或复制业务组件。
function leaf(id) { return { tabs: [id], active: id } }
function split(axis, fraction, a, b) { return {axis, fraction, a, b} }
function has(node, id) {
    return node.tabs ? node.tabs.includes(id) : has(node.a,id) || has(node.b,id)
}
function activate(node,id) {
    if (node.tabs) {
        if (node.tabs.includes(id)) node.active = id
    } else {
        activate(node.a,id)
        activate(node.b,id)
    }
}
function remove(node,id) {
    if (node.tabs) {
        const i = node.tabs.indexOf(id)
        if (i >= 0) {
            node.tabs.splice(i,1)
            if (node.active === id) node.active = node.tabs[Math.min(i,node.tabs.length-1)]
        }
        return node.tabs.length ? node : null
    }
    node.a = remove(node.a,id)
    node.b = remove(node.b,id)
    return node.a && node.b ? node : node.a || node.b
}
// 0 合并在目标之后，1/2 左右，3/4 上下，5 插在目标标签之前。
function place(tree, source, target, side) {
    if (source === target || !has(tree,source) || !has(tree,target)) return tree
    const next = remove(JSON.parse(JSON.stringify(tree)),source)
    function insert(node) {
        if (node.tabs) {
            if (!node.tabs.includes(target)) return node
            if (side === 0 || side === 5) {
                node.tabs.splice(node.tabs.indexOf(target) + (side === 0 ? 1 : 0),0,source)
                node.active = source
                return node
            }
            const before = side === 1 || side === 3
            return split(side <= 2 ? 1 : 2,0.5,before ? leaf(source) : node,before ? node : leaf(source))
        }
        node.a = insert(node.a)
        node.b = insert(node.b)
        return node
    }
    return insert(next)
}
function validate(tree, ids) {
    const seen = []
    function visit(node, depth) {
        if (!node || depth > ids.length) return false
        if (Array.isArray(node.tabs)) {
            if (!node.tabs.length || !node.tabs.includes(node.active)) return false
            for (const id of node.tabs) {
                if (!ids.includes(id) || seen.includes(id)) return false
                seen.push(id)
            }
            return true
        }
        return [1,2].includes(node.axis) && Number.isFinite(node.fraction)
            && node.fraction > 0 && node.fraction < 1 && visit(node.a,depth+1) && visit(node.b,depth+1)
    }
    return visit(tree,0) && seen.length === ids.length
}
function resize(tree,path,fraction) {
    const next = JSON.parse(JSON.stringify(tree))
    let node = next
    for (const part of path) node = node[part]
    node.fraction = Math.max(0.01,Math.min(0.99,fraction))
    return next
}
function geometry(tree, hidden, width, height, editing, sizes) {
    const result = {panels:{},groups:[],handles:[]}
    const gap = 7
    function measure(node,path) {
        if (node.tabs) {
            const tabs = node.tabs.filter(id => !hidden.includes(id))
            if (!tabs.length) return null
            let mw = 145, mh = 90
            for (const id of tabs) {
                const size = sizes[id]
                if (size) { mw = Math.max(mw,size[0]); mh = Math.max(mh,size[1]) }
            }
            return {tabs,active:tabs.includes(node.active) ? node.active : tabs[0],mw,mh}
        }
        const a = measure(node.a,path+"a"), b = measure(node.b,path+"b")
        if (!a || !b) return a || b
        const horizontal = node.axis === 1
        return {a,b,horizontal,path,fraction:node.fraction,
                mw: horizontal ? a.mw+b.mw+gap : Math.max(a.mw,b.mw),
                mh: horizontal ? Math.max(a.mh,b.mh) : a.mh+b.mh+gap}
    }
    function visit(node,x,y,w,h) {
        if (!node) return
        if (node.tabs) {
            const bar = editing || node.tabs.length > 1 ? Math.min(29,h) : 0
            result.groups.push({x,y,w,h,bar,tabs:node.tabs,active:node.active})
            result.panels[node.active] = {x,y:y+bar,w,h:h-bar}
            return
        }
        const hor = node.horizontal, total = Math.max(0,(hor?w:h)-gap)
        const minA = hor ? node.a.mw : node.a.mh, minB = hor ? node.b.mw : node.b.mh
        const scale = Math.min(1,total/(minA+minB))
        const low = Math.round(minA*scale), high = Math.max(low,total-Math.round(minB*scale))
        const first = Math.max(low,Math.min(high,Math.round(total*node.fraction)))
        result.handles.push({path:node.path,hor,total,low,high,origin:hor?x:y,
                             x:hor?x+first:x,y:hor?y:y+first,w:hor?gap:w,h:hor?h:gap})
        visit(node.a,x,y,hor?first:w,hor?h:first)
        visit(node.b,hor?x+first+gap:x,hor?y:y+first+gap,hor?total-first:w,hor?h:total-first)
    }
    if (tree) visit(measure(tree,""),0,0,Math.max(0,width),Math.max(0,height))
    return result
}

function appendTab(node,target,id) {
    if (node.tabs) {
        if (!node.tabs.includes(target)) return false
        node.tabs.push(id)
        return true
    }
    return appendTab(node.a,target,id) || appendTab(node.b,target,id)
}
