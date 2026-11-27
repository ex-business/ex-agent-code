#include "text_selection_rules.h"
#include <QTextBoundaryFinder>
#include <QGuiApplication>
#include <QStyleHints>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#ifdef Q_OS_MACOS
#include "platform/macos/word_boundaries.h"
#endif

namespace exagent {
bool TextSelectionRules::containsText(QQuickTextDocument *document, qreal x, qreal y) const
{
    if (!document || !document->textDocument()) return false;
    // 使用当前富文本排版精确命中，行尾留白和段落间距不能沿用最近字符的选择光标。
    return document->textDocument()->documentLayout()->hitTest(QPointF(x, y), Qt::ExactHit) >= 0;
}
int TextSelectionRules::doubleClickInterval() const { return QGuiApplication::styleHints()->mouseDoubleClickInterval(); }
int TextSelectionRules::dragDistance() const { return QGuiApplication::styleHints()->startDragDistance(); }
QVariantMap TextSelectionRules::range(const QString &text, int position, int unit, bool code) const
{
    if (text.isEmpty()) return {{"start", 0}, {"end", 0}};
    position = qBound(0, position, int(text.size()) - 1);
    if (text[position].isLowSurrogate() && position > 0) --position;
    int start = position;
    int end = position + 1;
    if (unit == 2) {
        // 三击以实际段落/代码行为单位，包含行尾换行，复制后保留原文结构。
        start = int(text.lastIndexOf('\n', position - 1)) + 1;
        if (position == 0) start = 0;
        const auto newline = text.indexOf('\n', position);
        end = newline < 0 ? int(text.size()) : int(newline + 1);
    } else {
#ifdef Q_OS_MACOS
        if (text[position].script() == QChar::Script_Han) {
            const auto native = macWordRange(text, position);
            if (native.first >= 0) return {{"start", native.first}, {"end", native.second}};
        }
#endif
        const auto identifier = [](QChar c) {
            return c.unicode() < 128 && (c.isLetterOrNumber() || c == '_');
        };
        if (code && identifier(text[position])) {
            // 代码标识符保留下划线；中文不使用“连续字母”扫描，避免整句被选中。
            while (start > 0 && identifier(text[start - 1])) --start;
            while (end < text.size() && identifier(text[end])) ++end;
        } else {
            QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
            finder.setPosition(position);
            if (!finder.isAtBoundary()) start = finder.toPreviousBoundary();
            finder.setPosition(position);
            end = finder.toNextBoundary();
            if (start < 0) start = 0;
            if (end < 0) end = int(text.size());
            // 空格、标点和 emoji 同样有明确选区，不能错误地选中左侧单词。
            if (!text[position].isLetterOrNumber() && !text[position].isSpace()) {
                QTextBoundaryFinder grapheme(QTextBoundaryFinder::Grapheme, text);
                grapheme.setPosition(position);
                start = grapheme.isAtBoundary() ? position : grapheme.toPreviousBoundary();
                grapheme.setPosition(position);
                end = grapheme.toNextBoundary();
            }
        }
    }
    return {{"start", qMax(0, start)}, {"end", qMax(start, end)}};
}
}
