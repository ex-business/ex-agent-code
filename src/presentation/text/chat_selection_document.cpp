#include "chat_selection_document.h"
#include <algorithm>

namespace exagent {
void ChatSelectionDocument::setModel(MessageListModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    clearSelection();
    m_model = model;
    if (model) {
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &ChatSelectionDocument::clearSelection);
        connect(model, &QAbstractItemModel::modelReset, this, &ChatSelectionDocument::rebuildRows);
        connect(model, &QAbstractItemModel::rowsInserted, this, &ChatSelectionDocument::rebuildRows);
        connect(model, &QAbstractItemModel::dataChanged, this, &ChatSelectionDocument::reconcilePositions);
        connect(model, &QObject::destroyed, this, [this] { m_model = nullptr; m_rows.clear(); clearSelection(); emit modelChanged(); });
    }
    rebuildRows();
    emit modelChanged();
}
void ChatSelectionDocument::rebuildRows()
{
    m_rows.clear();
    if (m_model) for (int row = 0; row < m_model->count(); ++row)
        m_rows.insert(m_model->data(m_model->index(row), MessageListModel::IdRole).toString(), row);
    reconcilePositions();
}
void ChatSelectionDocument::reconcilePositions()
{
    if (!m_model) return;
    for (auto *position : {&m_anchor, &m_cursor}) {
        if (position->id.isEmpty()) continue;
        const auto row = orderOf(position->id);
        if (row < 0) { clearSelection(); return; }
        const auto keys = m_model->selectionBlockKeys(row);
        const auto ordinal = keys.indexOf(position->blockKey);
        // 内容版本替换后旧块不存在时明确失效，不能把旧偏移投影到同序号的新块。
        if (ordinal < 0) { clearSelection(); return; }
        position->block = ordinal;
    }
    emit selectionChanged();
}
bool ChatSelectionDocument::less(const Position &a, const Position &b) const
{
    const int ar = orderOf(a.id), br = orderOf(b.id);
    return ar != br ? ar < br : a.block != b.block ? a.block < b.block : a.offset < b.offset;
}
int ChatSelectionDocument::selectionLength() const
{
    // 协调器只需要是否非空；完整文本仅在用户执行复制时生成。
    return orderOf(m_anchor.id) >= 0 && orderOf(m_cursor.id) >= 0 &&
        (m_anchor.id != m_cursor.id || m_anchor.block != m_cursor.block || m_anchor.offset != m_cursor.offset) ? 1 : 0;
}
void ChatSelectionDocument::select(const QString &anchorId, int anchorBlock, int anchorOffset,
                                   const QString &cursorId, int cursorBlock, int cursorOffset)
{
    if (orderOf(anchorId) < 0 || orderOf(cursorId) < 0) return;
    Position anchor{anchorId, qMax(0, anchorBlock), qMax(0, anchorOffset), {}};
    Position cursor{cursorId, qMax(0, cursorBlock), qMax(0, cursorOffset), {}};
    anchor.blockKey = m_model->selectionBlockKeys(orderOf(anchorId)).value(anchor.block);
    cursor.blockKey = m_model->selectionBlockKeys(orderOf(cursorId)).value(cursor.block);
    if (anchor.blockKey.isEmpty() || cursor.blockKey.isEmpty()) return;
    if (m_anchor.id == anchor.id && m_anchor.block == anchor.block && m_anchor.offset == anchor.offset &&
        m_cursor.id == cursor.id && m_cursor.block == cursor.block && m_cursor.offset == cursor.offset) return;
    m_anchor = anchor; m_cursor = cursor;
    emit selectionChanged();
}
QVariantMap ChatSelectionDocument::anchorPosition() const
{
    return {{"id", m_anchor.id}, {"block", m_anchor.block}, {"blockId", m_anchor.blockKey}, {"offset", m_anchor.offset}};
}
QVariantMap ChatSelectionDocument::range(const QString &id, int block, int length) const
{
    if (!selectionLength()) return {{"start", 0}, {"end", 0}};
    auto first = m_anchor, last = m_cursor;
    if (less(last, first)) std::swap(first, last);
    const Position start{id, block, 0, {}}, end{id, block, length, {}};
    if (orderOf(id) < 0 || less(end, first) || less(last, start)) return {{"start", 0}, {"end", 0}};
    return {{"start", first.id == id && first.block == block ? qBound(0, first.offset, length) : 0},
            {"end", last.id == id && last.block == block ? qBound(0, last.offset, length) : length}};
}
void ChatSelectionDocument::clearSelection()
{
    m_anchor = {}; m_cursor = {};
    emit selectionChanged();
}
QVariant ChatSelectionDocument::selectedContent() const
{
    if (!m_model || !selectionLength()) return QString{};
    auto first = m_anchor, last = m_cursor;
    if (less(last, first)) std::swap(first, last);
    QStringList pieces;
    // 仅复制覆盖的消息，未显示的历史页不预读；内容来自模型，不依赖存活的控件。
    for (int row = orderOf(first.id); row <= orderOf(last.id); ++row) {
        const auto id = m_model->data(m_model->index(row), MessageListModel::IdRole).toString();
        const auto blocks = m_model->selectionBlocks(row);
        for (int block = 0; block < blocks.size(); ++block) {
            const auto &text = blocks[block];
            const auto span = range(id, block, int(text.size()));
            const auto value = text.mid(span.value("start").toInt(), span.value("end").toInt() - span.value("start").toInt());
            if (!value.isEmpty()) pieces.append(value);
        }
    }
    auto text = pieces.join("\n\n");
    text.replace(QChar::ParagraphSeparator, '\n');
    text.replace(QChar::LineSeparator, '\n');
    // 富文本表格的内部框架标记不能进入剪贴板。
    text.replace(QChar(0xfdd0), '\t');
    text.replace(QChar(0xfdd1), '\n');
    return text;
}
void ChatSelectionDocument::selectLoaded()
{
    if (!m_model) return;
    int firstRow = -1;
    QStringList firstBlocks;
    // 全选只定位两端，不能为了确定范围预先解析所有已加载历史。
    for (int row = 0; row < m_model->count(); ++row) {
        firstBlocks = m_model->selectionBlocks(row);
        if (!firstBlocks.isEmpty()) { firstRow = row; break; }
    }
    if (firstRow < 0) return;
    const auto firstId = m_model->data(m_model->index(firstRow), MessageListModel::IdRole).toString();
    for (int row = m_model->count() - 1; row >= firstRow; --row) {
        const auto blocks = row == firstRow ? firstBlocks : m_model->selectionBlocks(row);
        if (blocks.isEmpty()) continue;
        const auto lastId = m_model->data(m_model->index(row), MessageListModel::IdRole).toString();
        select(firstId, 0, 0, lastId, int(blocks.size()) - 1, int(blocks.last().size()));
        return;
    }
}
}
