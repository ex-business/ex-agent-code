#include "text_edits.h"
#include <algorithm>
namespace exagent::lsp {
int offset(const QString &s, const QJsonObject &p) {
  auto l = p.value("line"), c = p.value("character");
  if (!l.isDouble() || !c.isDouble() || l.toDouble() != l.toInt(-1) ||
      c.toDouble() != c.toInt(-1))
    return -1;
  int line = l.toInt(-1), col = c.toInt(-1);
  if (line < 0 || col < 0)
    return -1;
  int start = 0;
  while (line-- > 0) {
    start = s.indexOf('\n', start);
    if (start < 0)
      return -1;
    ++start;
  }
  auto end = s.indexOf('\n', start);
  if (end < 0)
    end = s.size();
  if (col > end - start)
    return -1;
  const auto at = start + col;
  if (at > 0 && at < s.size() && s[at].isLowSurrogate() &&
      s[at - 1].isHighSurrogate())
    return -1;
  return at;
}
QJsonObject position(const QString &s, int at) {
  at = qBound(0, at, int(s.size()));
  int line = 0, start = 0;
  for (int i = 0; i < at; ++i)
    if (s[i] == '\n') {
      ++line;
      start = i + 1;
    }
  return {{"line", line}, {"character", at - start}};
}
bool edits(const QString &source, const QJsonArray &items, QVector<Edit> *out,
           QString *error) {
  out->clear();
  if (items.size() > 2000) {
    *error = QStringLiteral("一次修改超过2000处。");
    return false;
  }
  qint64 size = source.size();
  for (const auto &v : items) {
    const auto row = v.toObject(), range = row.value("range").toObject();
    const auto start = offset(source, range.value("start").toObject()),
               end = offset(source, range.value("end").toObject());
    if (start < 0 || end < start || !row.value("newText").isString()) {
      *error = QStringLiteral("语言服务返回了无效文本范围。");
      return false;
    }
    auto text = row.value("newText").toString();
    if (text.contains(QChar::Null) ||
        text.contains(QChar::ParagraphSeparator) ||
        text.contains(QChar::CarriageReturn)) {
      *error = QStringLiteral("语言服务修改含不支持的控制或换行字符。");
      return false;
    }
    size += text.size() - (end - start);
    if (size > 8 * 1024 * 1024) {
      *error = QStringLiteral("语言服务修改超过编辑容量。");
      return false;
    }
    out->append({start, end, text});
  }
  std::sort(out->begin(), out->end(), [](const Edit &a, const Edit &b) {
    return a.start == b.start ? a.end < b.end : a.start < b.start;
  });
  for (int i = 1; i < out->size(); ++i)
    if ((*out)[i].start < (*out)[i - 1].end ||
        (*out)[i].start == (*out)[i - 1].start) {
      *error = QStringLiteral("语言服务修改范围相互重叠。");
      return false;
    }
  return true;
}
} // namespace exagent::lsp
