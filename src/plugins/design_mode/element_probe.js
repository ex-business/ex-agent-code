/* 在 WebEngine 的 ApplicationWorld 中执行，坐标为可见视口的比例。 */
(function inspect(nx, ny, details) {
    const w = window.innerWidth, h = window.innerHeight;
    if (!w || !h) return null;
    const el = document.elementFromPoint(nx * w, ny * h);
    if (!el) return null;
    const box = el.getBoundingClientRect();
    const x = Math.max(0, box.left), y = Math.max(0, box.top);
    const right = Math.min(w, box.right), bottom = Math.min(h, box.bottom);
    if (right <= x || bottom <= y) return null;
    const rect = [x / w, y / h, (right - x) / w, (bottom - y) / h];
    if (!details) return {rect};
    const privateNodes = 'iframe,frame,object,embed,input:not([type=button]):not([type=submit]),textarea,select,[contenteditable]:not([contenteditable=false])';
    if (el.closest(privateNodes)) return {restricted: true};
    // 主文档无法完整检查自定义元素的封闭 shadow tree；不把未核实的组件或大容器截图带进草稿。
    const walker = document.createTreeWalker(el, NodeFilter.SHOW_ELEMENT);
    let node = el, count = 0;
    while (node) {
        if (++count > 256 || node.shadowRoot || node.localName.includes('-') || node.matches(privateNodes))
            return {restricted:true};
        node = walker.nextNode();
    }

    const escape = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    const tag = e => e.localName;
    function selector(e) {
        const parts = [];
        while (e && parts.length < 18) {
            const id = e.id && e.id.length <= 96 ? '#' + CSS.escape(e.id) : '';
            if (id && document.querySelectorAll(id).length === 1) { parts.unshift(id); break; }
            let index = 1;
            for (let prev = e.previousElementSibling; prev; prev = prev.previousElementSibling)
                if (prev.localName === e.localName) ++index;
            parts.unshift(CSS.escape(tag(e)) + ':nth-of-type(' + index + ')');
            e = e.parentElement;
        }
        return parts.join(' > ');
    }
    // 只描述可见标记的结构，不复制脚本、表单值、链接参数和隐藏子树。
    let budget = 22;
    function markup(e, depth) {
        if (!e || budget-- <= 0 || e.hidden || /^(script|style|template|noscript)$/i.test(tag(e))) return '';
        const css = getComputedStyle(e);
        if (css.display === 'none' || css.visibility !== 'visible' || css.contentVisibility === 'hidden' ||
                Number(css.opacity) === 0 || e.getAttribute('aria-hidden') === 'true') return '';
        let out = '<' + tag(e);
        for (const key of ['id','class','role','aria-label','data-testid']) {
            const v = e.getAttribute(key);
            if (v) out += ' ' + key + '="' + escape(v.slice(0,96)) + '"';
        }
        out += '>';
        if (depth > 0) {
            let child = e.firstChild;
            for (let i = 0; child && i < 12 && budget > 0; ++i, child = child.nextSibling) {
                if (child.nodeType === 3) out += escape(child.nodeValue.slice(0,100));
                else if (child.nodeType === 1) out += markup(child, depth - 1);
            }
        }
        return out + '</' + tag(e) + '>';
    }
    const styles = {}, computed = getComputedStyle(el);
    for (const name of ['display','position','width','height','margin','padding','gap','color','background-color',
            'font-family','font-size','font-weight','line-height','border','border-radius','align-items','justify-content'])
        styles[name] = computed.getPropertyValue(name).slice(0,160);
    let source = '';
    const owner = el.closest('[data-source-file],[data-v-inspector],[data-source-location]');
    if (owner) {
        source = owner.getAttribute('data-v-inspector') || owner.getAttribute('data-source-location') || owner.getAttribute('data-source-file') || '';
        // 开发工具有时提供带凭据或热更查询串的 URL；只保留源码路径和行列。
        if (/^[a-z][a-z0-9+.-]*:\/\//i.test(source)) {
            try { source = new URL(source).pathname; } catch (_) { source = ''; }
        }
        source = source.split(/[?#]/,1)[0].trim();
        const line = owner.getAttribute('data-source-line');
        if (line && /^[0-9]{1,8}$/.test(line) && owner.hasAttribute('data-source-file')) source += ':' + line;
    }
    const neighbors = [el.parentElement, el.previousElementSibling, el.nextElementSibling].map(node => node ? tag(node) : '');
    return {rect, tag:tag(el), selector:selector(el), markup:markup(el,3).slice(0,2400), styles,
            neighbors, source:source.slice(0,320)};
})
