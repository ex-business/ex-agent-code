#include "code_line_model.h"
#include "syntax_highlighter.h"

#include <QTimer>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <algorithm>
#include "presentation/text/text_selection_rules.h"
#include <QTextBoundaryFinder>

namespace exagent {
namespace {
constexpr int MaxRenderedLineCharacters = 24 * 1024;
}

CodeLineModel::CodeLineModel(QObject *parent) : QAbstractListModel(parent)
{
    m_indexSchedule.setSingleShot(true);
    connect(&m_indexSchedule, &QTimer::timeout, this, &CodeLineModel::startIndex);
}
CodeLineModel::~CodeLineModel() { if (m_indexCancel) m_indexCancel->store(true); }
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
    m_preferredX = -1;
    m_layoutRow = -1;
    m_hitLayout.reset();
    m_lines.clear();
    m_highlightCache.clear();
    m_formattedLineCount = 0;
    m_maximumColumns = 0;
    ++m_indexGeneration;
    if (m_indexCancel) m_indexCancel->store(true);
    m_preparing = !m_code.isEmpty();
    emit preparingChanged();
    m_indexSchedule.start(0);
}

void CodeLineModel::startIndex()
{
    // 同一控件只允许一个任务在途；反复更新时只保留最新快照，不堆积旧版本。
    if (m_indexRunning || m_code.isEmpty()) return;
    static QThreadPool pool;
    pool.setMaxThreadCount(2);
    pool.setThreadPriority(QThread::LowPriority);
    m_indexRunning = true;
    const auto generation = m_indexGeneration;
    const auto cancel = m_indexCancel = std::make_shared<std::atomic_bool>(false);
    auto *watcher = new QFutureWatcher<PreparedIndex>(this);
    connect(watcher, &QFutureWatcher<PreparedIndex>::finished, this, [this, watcher, generation, cancel] {
        m_indexRunning = false;
        if (generation == m_indexGeneration && !cancel->load()) {
            PreparedIndex result;
            try { result = watcher->result(); } catch (...) { qWarning("代码索引准备失败，原始代码仍可复制"); }
            beginResetModel();
            m_lines = std::move(result.lines);
            m_maximumColumns = result.maximumColumns;
            endResetModel();
            m_preparing = false;
            emit preparingChanged(); emit codeChanged(); emit metricsChanged();
        } else startIndex();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&pool, [code = m_code, language = m_language, dark = m_dark, cancel] {
        PreparedIndex result;
        qsizetype offset = 0;
        int state = 0;
        while (offset < code.size()) {
            if (cancel->load()) return PreparedIndex{};
            auto end = code.indexOf('\n', offset);
            if (end < 0) end = code.size();
            auto length = end - offset;
            if (length > 0 && code[offset + length - 1] == '\r') --length;
            const auto line = QStringView(code).mid(offset, length);
            result.lines.append({offset, length, state});
            // 跨行词法状态也在后台建立；可见行仍按需着色并使用原有有界缓存。
            state = SyntaxHighlighter::scan(line, language, state, dark, false).endState;
            int columns = 0;
            for (const auto c : line.left(MaxRenderedLineCharacters)) columns += c == '\t' ? 4 : 1;
            result.maximumColumns = qMax(result.maximumColumns, columns);
            offset = end + 1;
        }
        if (code.endsWith('\n')) result.lines.append({code.size(), 0, state});
        return result;
    }));
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
        if (c == '\t' && displayColumn - display < 2) break;
        display += c == '\t' ? 4 : 1;
        ++raw;
    }
    // 垂直移动和制表符映射也必须落在完整字素边界。
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, text.toString());
    boundary.setPosition(raw);
    if (!boundary.isAtBoundary()) raw = qMax(0, boundary.toPreviousBoundary());
    return line.offset + raw;
}

int CodeLineModel::positionAtPixel(int row, qreal x, const QFont &font, bool nearestCursor) const
{
    if (row < 0 || row >= m_lines.size()) return 0;
    const auto line = layoutLine(row, font);
    const int column = line.xToCursor(x, nearestCursor ? QTextLine::CursorBetweenCharacters : QTextLine::CursorOnCharacter);
    return positionAt(row, column);
}

int CodeLineModel::rowAt(int position) const
{
    const auto found = std::upper_bound(m_lines.cbegin(), m_lines.cend(), position,
        [](int value, const Line &line) { return value < line.offset; });
    return qMax(0, int(found - m_lines.cbegin()) - 1);
}
bool CodeLineModel::textAtPixel(int row, qreal x, const QFont &font) const
{
    if (row < 0 || row >= m_lines.size() || m_lines[row].length == 0 || x < 0) return false;
    return x <= layoutLine(row, font).naturalTextWidth();
}
QTextLine CodeLineModel::layoutLine(int row, const QFont &font) const
{
    // 拖动时复用当前行的字形布局，不为每个鼠标位置反复测量多个字符串前缀。
    if (!m_hitLayout || m_layoutRow != row || m_layoutFont != font) {
        const auto &line = m_lines[row];
        auto display = m_code.mid(line.offset, qMin<qsizetype>(line.length, MaxRenderedLineCharacters));
        display.replace('\t', "    ");
        m_hitLayout = std::make_unique<QTextLayout>(display, font);
        m_hitLayout->beginLayout();
        auto visual = m_hitLayout->createLine();
        if (visual.isValid()) visual.setLineWidth(1e9);
        m_hitLayout->endLayout();
        m_layoutRow = row;
        m_layoutFont = font;
    }
    return m_hitLayout->lineAt(0);
}
QVariantMap CodeLineModel::cursorGeometry(const QFont &font) const
{
    if (m_lines.isEmpty()) return {{"row", 0}, {"x", 0}};
    const int row = rowAt(m_cursor);
    int column = 0;
    const auto &line = m_lines[row];
    for (auto c : QStringView(m_code).mid(line.offset, qMin<qsizetype>(m_cursor - line.offset, MaxRenderedLineCharacters))) column += c == '\t' ? 4 : 1;
    return {{"row", row}, {"x", layoutLine(row, font).cursorToX(column)}};
}

void CodeLineModel::select(int anchor, int cursor)
{
    anchor = qBound(0, anchor, static_cast<int>(m_code.size()));
    cursor = qBound(0, cursor, static_cast<int>(m_code.size()));
    if (anchor == m_anchor && cursor == m_cursor) return;
    m_preferredX = -1;
    const int firstOffset = qMin(qMin(anchor, cursor), qMin(m_anchor, m_cursor));
    const int lastOffset = qMax(qMax(anchor, cursor), qMax(m_anchor, m_cursor));
    m_anchor = anchor;
    m_cursor = cursor;
    // 只通知受选择变化影响的行；虚拟列表仅刷新已创建的行对象。
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
    const int row = qMax(0, int(found - m_lines.cbegin()) - 1);
    const auto &line = m_lines[row];
    // 三击整行保留完整 CRLF，不能只复制 CR 而遗漏 LF。
    if (unit == 2) return {{"start", int(line.offset)}, {"end", row + 1 < m_lines.size() ? int(m_lines[row + 1].offset) : int(m_code.size())}};
    // 选词只分析命中的一行，拖动大代码选区不重复扫描整份文件。
    auto range = TextSelectionRules().range(m_code.mid(line.offset, line.length), position - line.offset, unit, true);
    range["start"] = range["start"].toInt() + int(line.offset);
    range["end"] = range["end"].toInt() + int(line.offset);
    return range;
}

void CodeLineModel::moveCursor(int key, bool extend, bool byWord, const QFont &font)
{
    if (m_lines.isEmpty()) return;
    int target = m_cursor;
    const int row = rowAt(m_cursor);
    qreal preferredX = -1;
    if (!extend && m_anchor != m_cursor && (key == Qt::Key_Left || key == Qt::Key_Right)) {
        target = key == Qt::Key_Left ? qMin(m_anchor, m_cursor) : qMax(m_anchor, m_cursor);
    } else if (key == Qt::Key_Home) target = m_lines[row].offset;
    else if (key == Qt::Key_End) target = m_lines[row].offset + m_lines[row].length;
    else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        // 按实际字形横坐标保持目标位置，中文、emoji 和组合字符不能按字符数猜列宽。
        preferredX = m_preferredX >= 0 ? m_preferredX : cursorGeometry(font).value("x").toReal();
        const int targetRow = qBound(0, row + (key == Qt::Key_Up ? -1 : 1), int(m_lines.size()) - 1);
        target = positionAtPixel(targetRow, preferredX, font);
    } else {
        // 光标按字素移动，不能把 emoji 或代理对从中间截断。
        // 方向键只分析当前行；长代码不能每次按键都扫描整份文件。
        const auto &line = m_lines[row];
        const auto text = m_code.mid(line.offset, line.length);
        QTextBoundaryFinder finder(byWord ? QTextBoundaryFinder::Word : QTextBoundaryFinder::Grapheme, text);
        finder.setPosition(qMin(int(line.length), m_cursor - int(line.offset)));
        const int local = key == Qt::Key_Left ? finder.toPreviousBoundary() : finder.toNextBoundary();
        if (local >= 0) target = int(line.offset) + local;
        else if (key == Qt::Key_Left) target = row > 0 ? int(m_lines[row - 1].offset + m_lines[row - 1].length) : 0;
        else target = row + 1 < m_lines.size() ? int(m_lines[row + 1].offset) : int(m_code.size());
    }
    select(extend ? m_anchor : target, target);
    m_preferredX = preferredX;
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
