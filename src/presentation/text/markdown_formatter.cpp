#include "markdown_formatter.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>
#include <QRegularExpression>

namespace exagent {
QVariantMap MarkdownFormatter::userPresentation(const QString &content, const QFont &font) const
{
    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    // 用户输入作为字面文本生成 HTML，让显示与复制保留标记和换行。
    document.setPlainText(content);
    document.setTextWidth(-1);
    auto html = document.toHtml();
    html.replace(QRegularExpression("<body[^>]*>"), "<body>");
    html.replace(QRegularExpression("margin-(?:top|bottom):[^;]+;"), "margin-top:0px; margin-bottom:0px;");
    return {{"html", html}, {"width", document.idealWidth()}};
}
namespace {
QVariantMap proseHtml(const QString &text, bool dark)
{
    QTextDocument document;
    QFont font;
    font.setPixelSize(14);
    document.setDefaultFont(font);
    document.setMarkdown(text);
    struct Span { int start; int length; QTextCharFormat format; };
    QList<Span> codeSpans;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            if (format.fontFixedPitch() || format.font().styleHint() == QFont::TypeWriter
                || format.font().families().join(' ').contains("mono", Qt::CaseInsensitive)) {
                auto code = format;
                code.setBackground(QColor(dark ? "#343434" : "#ededed"));
                code.setForeground(QColor(dark ? "#ececec" : "#303030"));
                codeSpans.append({fragment.position(), fragment.length(), code});
            }
        }
    }
    // 在文档语义层处理行内代码，避免正则误改正文或依赖某种 HTML 字体拼写。
    for (const auto &span : codeSpans) {
        QTextCursor cursor(&document);
        cursor.setPosition(span.start);
        cursor.setPosition(span.start + span.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(span.format);
    }
    QVariantList rules;
    const auto styleTables = [&](auto &&self, QTextFrame *frame) -> void {
        for (auto *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                auto format = table->format();
                format.setBorder(0); format.setCellSpacing(0); format.setCellPadding(8);
                format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                table->setFormat(format);
                for (int row = 0; row < table->rows(); ++row) {
                    const int end = table->cellAt(row, table->columns() - 1).lastCursorPosition().position();
                    const int next = row + 1 < table->rows() ? table->cellAt(row + 1, 0).firstCursorPosition().position() : table->lastCursorPosition().position() + 1;
                    rules.append(QVariantMap{{"end", end}, {"next", next}});
                }
                for (int row = 0; row < table->rows(); ++row) for (int column = 0; column < table->columns(); ++column) {
                    auto cell = table->cellAt(row, column);
                    auto cellFormat = cell.format().toTableCellFormat();
                    cellFormat.setBorder(0);
                    cellFormat.setBottomBorder(1);
                    cellFormat.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cellFormat.setBottomBorderBrush(QColor(dark ? "#3c3c3c" : "#e7e7e7"));
                    cell.setFormat(cellFormat);
                }
            }
            self(self, child);
        }
    };
    styleTables(styleTables, document.rootFrame());
    auto html = document.toHtml();
    html.replace(QRegularExpression("<body[^>]*>"), "<body>");
    html.replace(QRegularExpression("margin-top:[^;]+;"), "margin-top:0px;");
    html.replace(QRegularExpression("margin-bottom:[^;]+;"), "margin-bottom:8px;");
    html.replace(QRegularExpression("(<(?:p|li)[^>]*style=\")"), "\\1line-height:150%; ");
    // HTML 重载可能调整表格前后的段落位置；装饰线必须按实际展示文档重新取坐标。
    if (!rules.isEmpty()) {
        QTextDocument rendered;
        rendered.setHtml(html);
        rules.clear();
        const auto collect = [&](auto &&self, QTextFrame *frame) -> void {
        for (auto *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) for (int row = 0; row < table->rows(); ++row) {
                const int end = table->cellAt(row, table->columns() - 1).lastCursorPosition().position();
                const int next = row + 1 < table->rows() ? table->cellAt(row + 1, 0).firstCursorPosition().position() : end;
                rules.append(QVariantMap{{"end", end}, {"next", next}});
            }
            self(self, child);
        }
    };
        collect(collect, rendered.rootFrame());
    }
    return {{"html", html}, {"tableRules", rules}};
}
}
QVariantList MarkdownFormatter::split(const QString &content) const
{
    QVariantList result;
    qsizetype start = 0;
    qsizetype offset = 0;
    QString language;
    QString fence;
    bool inCode = false;
    int codeLines = 0;
    const auto append = [&](qsizetype end) {
        auto text = content.mid(start, end - start);
        if (text.endsWith('\n')) text.chop(1);
        if (!inCode) text = text.trimmed();
        if (text.isEmpty() && !inCode) return;
        QVariantMap block;
        block.insert("kind", inCode ? 1 : 0);
        block.insert("text", text);
        block.insert("language", language);
        // 分割围栏时已经逐行经过代码；复用行数预留视口，无须等待高亮索引或再次扫描原文。
        if (inCode) block.insert("codeLineCount", text.isEmpty() ? 0 : codeLines);
        result.append(block);
    };
    static const QRegularExpression opening("^ {0,3}(`{3,}|~{3,})([^`]*)$");
    while (offset < content.size()) {
        auto end = content.indexOf('\n', offset);
        if (end < 0) end = content.size();
        const auto line = content.mid(offset, end - offset);
        const auto match = opening.match(line);
        // 结束围栏必须同字符且至少同长度，代码中的 ``` 不能提前结束四反引号围栏。
        const bool closing = inCode && line.trimmed().size() >= fence.size()
            && line.trimmed() == QString(line.trimmed().size(), fence.front());
        if ((!inCode && match.hasMatch()) || closing) {
            append(offset);
            if (!inCode) { fence = match.captured(1); language = match.captured(2).trimmed(); }
            inCode = !inCode;
            start = end + 1;
            codeLines = 0;
        } else if (inCode) {
            ++codeLines;
        }
        offset = end + 1;
    }
    append(content.size());
    return result;
}
QVariantMap MarkdownFormatter::prosePresentation(const QString &content, bool dark) const { return proseHtml(content, dark); }
QVariantList MarkdownFormatter::format(const QString &content, bool dark) const
{
    auto blocks = split(content);
    for (auto &entry : blocks) {
        auto block = entry.toMap();
        if (block.value("kind").toInt() == 0) {
            const auto prepared = proseHtml(block.value("text").toString(), dark);
            for (auto it = prepared.cbegin(); it != prepared.cend(); ++it) block.insert(it.key(), it.value());
        }
        entry = block;
    }
    return blocks;
}

}
