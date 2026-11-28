#include "editor_page.h"
#include "editor_document.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
namespace exagent {
EditorPage::EditorPage(EditorDocument *d) : QObject(d), document(d) {
  auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPixelSize(13);
  window.setDefaultFont(font);
  window.setDocumentMargin(0);
  auto options = window.defaultTextOption();
  options.setWrapMode(QTextOption::NoWrap);
  options.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
  window.setDefaultTextOption(options);
  window.setUndoRedoEnabled(false);
  connect(&window, &QTextDocument::contentsChanged, this, &EditorPage::edited);
  connect(document, &EditorDocument::contentChanged, this, [this] {
    if (!updating)
      load(qMin(page, count() - 1), start + localCursor);
  });
  load(0);
}
EditorPage::~EditorPage() {
  if (view && view->textDocument() == &window)
    view->setTextDocument(new QTextDocument(view));
}
int EditorPage::count() const {
  return qMax(1, (document->characterCount() + Chunk - 1) / Chunk);
}
int EditorPage::firstLine() const { return document->lineAt(start) + 1; }
int EditorPage::lines() const { return document->lineCount(); }
void EditorPage::attach(QQuickTextDocument *v) {
  if (!v)
    return;
  if (view && view != v && view->textDocument() == &window)
    view->setTextDocument(new QTextDocument(view));
  view = v;
  view->setTextDocument(&window);
  emit windowChanged();
}
void EditorPage::show(int number) { load(qBound(0, number - 1, count() - 1)); }
void EditorPage::goToLine(int line) {
  locate(document->lineOffset(qBound(0, line - 1, lines() - 1)));
}
void EditorPage::recordCursor(int at) {
  if (updating)
    return;
  localCursor = qBound(0, at, int(baseline.size()));
  document->recordCaret(start + localCursor, start + localCursor);
}
void EditorPage::locate(int at) {
  at = qBound(0, at, document->characterCount());
  load(qMin(at / Chunk, count() - 1), at);
}
void EditorPage::load(int index, int globalCursor) {
  updating = true;
  page = qBound(0, index, count() - 1);
  start = page * Chunk;
  int end = qMin(document->characterCount(), start + Chunk);
  // 分页边界不能拆开emoji等代理对，界面窗口变化也不能插入额外换行。
  if (document->lowSurrogateAt(start))
    ++start;
  if (document->lowSurrogateAt(end))
    ++end;
  baseline = document->rangeText(start, end);
  window.setPlainText(baseline);
  window.setModified(false);
  localCursor = globalCursor < 0
                    ? 0
                    : qBound(0, globalCursor - start, int(baseline.size()));
  message.clear();
  updating = false;
  emit changed();
  emit windowChanged();
}
void EditorPage::edited() {
  if (updating)
    return;
  const auto now =
      window.toRawText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
  if (now == baseline)
    return;
  updating = true;
  if (!document->replaceWindow(start, baseline, now)) {
    message =
        QStringLiteral("页面与完整缓冲不同步，请保留当前内容后重新定位。");
    updating = false;
    emit changed();
    return;
  }
  baseline = now;
  message.clear();
  updating = false;
  emit changed();
  // 大段粘贴先完整进入原缓冲，再回到有界排版窗口，不能截断用户输入。
  if (now.size() > 2 * Chunk)
    QTimer::singleShot(0, this, [this] { locate(start + localCursor); });
}
} // namespace exagent
