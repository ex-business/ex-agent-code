.pragma library

// 仅为阅读解开常见的 shell -lc 包装，执行与复制始终使用保存的原文。
function body(raw) {
    const value = raw.trim()
    const wrapper = /^(?:\/[^\s]*\/)?(?:bash|sh|zsh)\s+-l?c\s+/.exec(value)
    if (!wrapper) return value
    const argument = value.slice(wrapper[0].length)
    if (/^'[^']*'$/.test(argument)) return argument.slice(1, -1)
    if (/^"(?:[^"\\]|\\.)*"$/.test(argument))
        return argument.slice(1, -1).replace(/\\(["\\$`])/g, "$1")
    return value
}

function readable(raw) {
    const text = body(raw)
    // 只给简单单行命令链排版。管道、引用和脚本控制结构按原有顺序展示。
    if (text.includes("\n")) return text
    let result = "", quote = ""
    for (let i = 0; i < text.length; ++i) {
        const ch = text[i]
        if (ch === "\\" && quote !== "'" && i + 1 < text.length) {
            result += ch + text[++i]
        } else if (quote) {
            result += ch
            if (ch === quote) quote = ""
        } else if (ch === "'" || ch === '"') {
            quote = ch
            result += ch
        } else if ("()`#".includes(ch) || ["<<", ";;", ";&"].includes(text.slice(i, i + 2))) {
            return text
        } else if (ch === ";" && text[i + 1] === " ") {
            result += ";\n"
            while (text[i + 1] === " ") ++i
        } else result += ch
    }
    return quote ? text : result
}
