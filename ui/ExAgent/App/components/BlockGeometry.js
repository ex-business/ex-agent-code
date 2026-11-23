.pragma library

var codeLineHeight = 22
var codeHeaderHeight = 38
var codeFooterHeight = 27
var inlineCodeLimit = 330

function codeViewport(lines, limit) {
    return Math.min(limit, Math.max(1, lines) * codeLineHeight + 24)
}

function codeHeight(lines, limit) {
    return codeHeaderHeight + codeViewport(lines, limit) + (lines > 15 ? codeFooterHeight : 0)
}

// 同一正文在加载器创建前后沿用同一估算；限制换行扫描量，避免占用滚动帧。
function estimatedProse(text, width) {
    const newlines = (text.slice(0, 8192).match(/\n/g) || []).length
    return Math.max(24, Math.min(2400, 22 * Math.max(newlines + 1, Math.ceil(text.length / Math.max(20, width / 7)))))
}
