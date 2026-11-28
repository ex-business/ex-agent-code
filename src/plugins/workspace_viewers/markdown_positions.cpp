#include "markdown_positions.h"
#include "presentation/text/markdown_formatter.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
namespace exagent {
QVariantList prepareWorkspaceMarkdown(const QString &source, bool dark) {
  MarkdownFormatter formatter;
  auto blocks = formatter.format(source, dark);
  qsizetype offset = 0;
  int line = 1;
  QHash<QString, int> headings;
  QSet<QString> usedAnchors;
  for (auto &value : blocks) {
    auto block = value.toMap();
    const auto text = block.value("text").toString();
    const auto found = source.indexOf(text, offset);
    const auto start = found < 0 ? offset : found;
    line += QStringView(source).mid(offset, start - offset).count(u'\n');
    const auto lines = text.split('\n');
    QVariantList positions, anchors;
    int position = 0;
    const bool code = block.value("kind").toInt() == 1;
    QTextDocument rendered;
    if (!code)
      rendered.setHtml(block.value("html").toString());
    // 光标位置以文档原始 UTF-16 为准；toPlainText 会折叠表格框架字符，不能用于行坐标。
    auto plain = rendered.toRawText();
    plain.replace(QChar::ParagraphSeparator, '\n');
    plain.replace(QChar::LineSeparator, '\n');
    plain.replace(QChar::Nbsp, ' ');
    int search = 0;
    for (int i = 0; i < lines.size(); ++i) {
      if (code)
        position = (i == 0 ? 0 : position + lines[i - 1].size() + 1);
      else {
        // 按语义文本定位源码行，软换行/空行映射到相邻字符，不用高度百分比猜位置。
        QString input = lines[i];
        if (input.contains('|')) {
          auto cells = input.split('|');
          while (!cells.isEmpty() && cells.first().trimmed().isEmpty())
            cells.removeFirst();
          if (!cells.isEmpty() && !QRegularExpression("^\\s*:?-+:?\\s*$")
                                       .match(cells.first())
                                       .hasMatch())
            input = cells.first();
        }
        QTextDocument fragment;
        fragment.setMarkdown(input);
        auto visible = fragment.toPlainText().trimmed();
        visible.replace(QChar::Nbsp, ' ');
        const auto match =
            visible.isEmpty() ? -1 : plain.indexOf(visible, search);
        if (match >= 0) {
          position = match;
          search = match + visible.size();
        } else
          position = qMin(search, int(plain.size()));
      }
      positions.append(QVariantMap{{"line", line + i}, {"position", position}});
    }
    if (!code) {
      for (auto b = rendered.begin(); b.isValid(); b = b.next())
        if (b.blockFormat().headingLevel() > 0) {
          auto slug = b.text().toLower().trimmed();
          slug.remove(QRegularExpression("[^\\p{L}\\p{M}\\p{N}_\\s-]"));
          slug.replace(QRegularExpression("\\s"), "-");
          int duplicate = headings.value(slug, 0);
          auto name =
              duplicate ? slug + '-' + QString::number(duplicate) : slug;
          while (usedAnchors.contains(name))
            name = slug + '-' + QString::number(++duplicate);
          headings[slug] = duplicate + 1;
          usedAnchors.insert(name);
          int headingLine = line;
          for (const auto &p : positions)
            if (p.toMap().value("position").toInt() <= b.position())
              headingLine = p.toMap().value("line").toInt();
          anchors.append(QVariantMap{{"name", name},
                                     {"position", b.position()},
                                     {"line", headingLine}});
        }
    }
    block.insert("sourceLine", line);
    block.insert("lineMap", positions);
    block.insert("anchors", anchors);
    value = block;
    offset = start + text.size();
    line += text.count('\n');
  }
  return blocks;
}
} // namespace exagent
