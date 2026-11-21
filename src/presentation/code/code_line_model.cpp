#include "code_line_model.h"
#include "syntax_highlighter.h"

#include <QTimer>
#include <algorithm>
#include <QFontMetricsF>
#include "presentation/text/text_selection_rules.h"
#include <QTextBoundaryFinder>

namespace exagent {
namespace {
constexpr int MaxRenderedLineCharacters = 24 * 1024;
}

CodeLineModel::CodeLineModel(QObject *parent) : QAbstractListModel(parent) {}
QString CodeLineModel::code() const { return m_code; }
QString CodeLineModel::language() const { return m_language; }
bool CodeLineModel::dark() const { return m_dark; }
int CodeLineModel::lineCount() const { return m_lines.size(); }
int CodeLineModel::maximumColumns() const { return m_maximumColumns; }
int CodeLineModel::cachedLineCount() const { return m_highlightCache.size(); }
int CodeLineModel::formattedLineCount() const { return m_formattedLineCount; }
int CodeLineModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_lines.size(); }

QVariant CodeLineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) return {};
    const auto &line = m_lines[index.row()];
    if (role == NumberRole) return index.row() + 1;
    const auto view = QStringView(m_code).mid(line.offset, line.length);
    if (role == SelectionFromRole || role == SelectionToRole) {
        const auto start = qMin(m_anchor, m_cursor);
        const auto end = qMax(m_anchor, m_cursor);
        if (start == end || end <= line.offset || start >= line.offset + line.length) return 0;
        const auto raw = qBound<qsizetype>(0, (role == SelectionFromRole ? start : end) - line.offset, line.length);
        int display = 0;
        for (const auto c : view.left(raw)) display += c == '\t' ? 4 : 1;
        return display;
    }
    if (role == PlainRole) return view.toString();
    if (role != HtmlRole) return {};
    if (const auto *cached = m_highlightCache.object(index.row())) return *cached;
    // 高亮仅在可见 delegate 请求此行时生成，不构造整段代码的富文本。
    const auto visible = view.left(MaxRenderedLineCharacters);
    auto html = SyntaxHighlighter::scan(visible, m_language, line.state, m_dark, true).html;
    if (view.size() > visible.size()) html += "&nbsp;…&nbsp;[超长单行，复制可获取完整内容]";
    const int cacheCost = qMax(1, static_cast<int>((html.size() * 2 + 1023) / 1024));
    m_highlightCache.insert(index.row(), new QString(html), cacheCost);
    ++m_formattedLineCount;
    queueMetrics();
    return html;
}

QHash<int, QByteArray> CodeLineModel::roleNames() const
{
    return {{PlainRole, "plainLine"}, {HtmlRole, "highlightedLine"}, {NumberRole, "lineNumber"},
            {SelectionFromRole, "selectionFrom"}, {SelectionToRole, "selectionTo"}};
}

void CodeLineModel::setCode(const QString &code)
{
    if (code == m_code) return;
    beginResetModel();
    m_code = code;
    rebuildIndex();
    endResetModel();
    emit codeChanged();
    emit selectionChanged();
    emit metricsChanged();
}

void CodeLineModel::setLanguage(const QString &language)
{
    const auto normalized = SyntaxHighlighter::normalizeLanguage(language);
    if (normalized == m_language) return;
    beginResetModel();
    m_language = normalized;
    rebuildIndex();
    endResetModel();
    emit languageChanged();
    emit selectionChanged();
    emit codeChanged();
    emit metricsChanged();
}

void CodeLineModel::setDark(bool dark)
{
    if (m_dark == dark) return;
    m_dark = dark;
    m_highlightCache.clear();
    if (!m_lines.isEmpty()) emit dataChanged(index(0), index(m_lines.size() - 1), {HtmlRole});
    emit darkChanged();
    queueMetrics();
}

void CodeLineModel::rebuildIndex()
{
    m_anchor = m_cursor = 0;
    m_lines.clear();
    m_highlightCache.clear();
    m_formattedLineCount = 0;
    m_maximumColumns = 0;
    if (m_code.isEmpty()) return;
    qsizetype offset = 0;
    int state = 0;
    while (offset < m_code.size()) {
        auto end = m_code.indexOf('\n', offset);
        if (end < 0) end = m_code.size();
        auto length = end - offset;
        if (length > 0 && m_code[offset + length - 1] == '\r') --length;
        const auto line = QStringView(m_code).mid(offset, length);
        m_lines.append({offset, length, state});
        // 一次性只建立轻量行索引和跨行词法状态，避免跳到注释中部时错误着色。
        state = SyntaxHighlighter::scan(line, m_language, state, m_dark, false).endState;
        int columns = 0;
        for (const auto c : line.left(MaxRenderedLineCharacters)) columns += c == '\t' ? 4 : 1;
        m_maximumColumns = qMax(m_maximumColumns, columns);
        offset = end + 1;
    }
}

QString CodeLineModel::selectedText() const { return m_code.mid(qMin(m_anchor, m_cursor), qAbs(m_cursor - m_anchor)); }
// 界面只查询选区长度，点击复制时才分配完整选区字符串。
int CodeLineModel::selectionLength() const { return qAbs(m_cursor - m_anchor); }
int CodeLineModel::selectionAnchor() const { return m_anchor; }
int CodeLineModel::selectionCursor() const { return m_cursor; }

int CodeLineModel::positionAt(int row, int displayColumn) const
{
    if (m_lines.isEmpty()) return 0;
    const auto &line = m_lines[qBound(0, row, static_cast<int>(m_lines.size()) - 1)];
    const auto text = QStringView(m_code).mid(line.offset, line.length);
    int display = 0;
    int raw = 0;
    for (const auto c : text) {
        if (display >= displayColumn) break;
        display += c == '\t' ? 4 : 1;
        ++raw;
    }
    return line.offset + raw;
}

int CodeLineModel::positionAtPixel(int row, qreal x, const QFont &font, bool nearestCursor) const
{
    if (row < 0 || row >= m_lines.size()) return 0;
    const auto &line = m_lines[row];
    auto display = m_code.mid(line.offset, qMin<qsizetype>(line.length, MaxRenderedLineCharacters));
    display.replace('\t', "    ");
    QFontMetricsF metrics(font);
    int lower = 0;
    int upper = display.size();
    // 按实际字体宽度命中，中文注释、制表符和代理对不能用平均字符宽度猜测。
    while (lower < upper) {
        const int middle = (lower + upper + 1) / 2;
        if (metrics.horizontalAdvance(display, middle) <= x) lower = middle;
        else upper = middle - 1;
    }
    if (nearestCursor && lower < display.size()) {
        const auto left = metrics.horizontalAdvance(display, lower);
        const auto right = metrics.horizontalAdvance(display, lower + 1);
        if (x - left > (right - left) / 2) ++lower;
    }
    if (lower > 0 && lower < display.size() && display[lower].isLowSurrogate()) --lower;
    return positionAt(row, lower);
}

void CodeLineModel::select(int anchor, int cursor)
{
    anchor = qBound(0, anchor, static_cast<int>(m_code.size()));
    cursor = qBound(0, cursor, static_cast<int>(m_code.size()));
    if (anchor == m_anchor && cursor == m_cursor) return;
    const int firstOffset = qMin(qMin(anchor, cursor), qMin(m_anchor, m_cursor));
    const int lastOffset = qMax(qMax(anchor, cursor), qMax(m_anchor, m_cursor));
    m_anchor = anchor;
    m_cursor = cursor;
    // 只通知受选择变化影响的行；虚拟列表仅刷新已创建的行对象。
    const auto rowAt = [this](int offset) {
        const auto found = std::upper_bound(m_lines.cbegin(), m_lines.cend(), offset,
            [](int value, const Line &line) { return value < line.offset; });
        return qMax(0, static_cast<int>(found - m_lines.cbegin()) - 1);
    };
    const int firstRow = rowAt(firstOffset);
    const int lastRow = rowAt(lastOffset);
    if (!m_lines.isEmpty()) emit dataChanged(index(firstRow), index(lastRow), {SelectionFromRole, SelectionToRole});
    emit selectionChanged();
}

void CodeLineModel::selectAll() { select(0, m_code.size()); }
void CodeLineModel::selectWord(int position)
{
    const auto range = rangeAt(position, 1);
    select(range.value("start").toInt(), range.value("end").toInt());
}

QVariantMap CodeLineModel::rangeAt(int position, int unit) const
{
    if (m_lines.isEmpty()) return {{"start", 0}, {"end", 0}};
    position = qBound(0, position, int(m_code.size()));
    const auto found = std::upper_bound(m_lines.cbegin(), m_lines.cend(), position,
        [](int value, const Line &line) { return value < line.offset; });
    const auto &line = m_lines[qMax(0, int(found - m_lines.cbegin()) - 1)];
    if (unit == 2) return {{"start", int(line.offset)}, {"end", qMin(int(m_code.size()), int(line.offset + line.length + 1))}};
    // 选词只分析命中的一行，拖动大代码选区不重复扫描整份文件。
    auto range = TextSelectionRules().range(m_code.mid(line.offset, line.length), position - line.offset, unit, true);
    range["start"] = range["start"].toInt() + int(line.offset);
    range["end"] = range["end"].toInt() + int(line.offset);
    return range;
}

void CodeLineModel::moveCursor(int key, bool extend, bool byWord)
{
    if (m_lines.isEmpty()) return;
    int target = m_cursor;
    const auto found = std::upper_bound(m_lines.cbegin(), m_lines.cend(), m_cursor,
        [](int value, const Line &line) { return value < line.offset; });
    const int row = qMax(0, int(found - m_lines.cbegin()) - 1);
    if (!extend && m_anchor != m_cursor && (key == Qt::Key_Left || key == Qt::Key_Right)) {
        target = key == Qt::Key_Left ? qMin(m_anchor, m_cursor) : qMax(m_anchor, m_cursor);
    } else if (key == Qt::Key_Home) target = m_lines[row].offset;
    else if (key == Qt::Key_End) target = m_lines[row].offset + m_lines[row].length;
    else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        int column = 0;
        for (auto c : QStringView(m_code).mid(m_lines[row].offset, m_cursor - m_lines[row].offset)) column += c == '\t' ? 4 : 1;
        target = positionAt(qBound(0, row + (key == Qt::Key_Up ? -1 : 1), int(m_lines.size()) - 1), column);
    } else {
        // 光标按字素移动，不能把 emoji 或代理对从中间截断。
        QTextBoundaryFinder finder(byWord ? QTextBoundaryFinder::Word : QTextBoundaryFinder::Grapheme, m_code);
        finder.setPosition(m_cursor);
        target = key == Qt::Key_Left ? finder.toPreviousBoundary() : finder.toNextBoundary();
        if (target < 0) target = key == Qt::Key_Left ? 0 : int(m_code.size());
    }
    select(extend ? m_anchor : target, target);
}

void CodeLineModel::queueMetrics() const
{
    if (m_metricsPending) return;
    m_metricsPending = true;
    QTimer::singleShot(0, this, [this] {
        m_metricsPending = false;
        emit const_cast<CodeLineModel *>(this)->metricsChanged();
    });
}

} // namespace exagent
