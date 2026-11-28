#include "terminal_links.h"
#include <QUrl>
#include <QVector>
#ifdef Q_OS_MACOS
#include <vterm.h>
#endif
namespace exagent {
QString TerminalLinks::safeUrl(const QString &text) {
  if (text.size() > 8192 || text.contains(QChar::ReplacementCharacter))
    return {};
  for (auto ch : text)
    if (ch.isSpace() || ch.category() == QChar::Other_Control)
      return {};
  const QUrl url(text, QUrl::StrictMode);
  return url.isValid() && !url.host().isEmpty() &&
                 (url.scheme() == "https" || url.scheme() == "http")
             ? text
             : QString();
}
struct TerminalLinks::State {
#ifdef Q_OS_MACOS
  VTerm *vt = nullptr;
  int rows = 0, cols = 0;
  QVector<int> grid[2];
  QVector<QString> urls{QString()};
  int urlBytes = 0;
  int screen = 0;
  int active = 0;
  QByteArray fragment;
  bool overflow = false;
  static int move(VTermRect dest, VTermRect src, void *data) {
    auto &s = *static_cast<State *>(data);
    // 区域可能重叠；只复制受影响的矩形，不能原地覆盖后再读取。
    QVector<int> copy;
    for (int y = src.start_row; y < src.end_row; ++y)
      for (int x = src.start_col; x < src.end_col; ++x)
        copy.append(s.cell(y, x));
    int i = 0;
    for (int y = dest.start_row; y < dest.end_row; ++y)
      for (int x = dest.start_col; x < dest.end_col; ++x) {
        if (i < copy.size())
          s.set(y, x, copy[i]);
        ++i;
      }
    return 1;
  }
  static int erase(VTermRect r, int, void *data) {
    auto &s = *static_cast<State *>(data);
    for (int y = r.start_row; y < r.end_row; ++y)
      for (int x = r.start_col; x < r.end_col; ++x)
        s.set(y, x, 0);
    return 1;
  }
  int cell(int y, int x) const {
    return y >= 0 && y < rows && x >= 0 && x < cols ? grid[screen][y * cols + x]
                                                    : 0;
  }
  int intern(const QString &url) {
    if (url.isEmpty())
      return 0;
    const int old = urls.indexOf(url);
    if (old >= 0)
      return old;
    // 每个附接客户端的地址表有界；超限不再接受新地址，不影响终端正文。
    if (urls.size() >= 1024 || urlBytes + url.size() * 2 > 1024 * 1024)
      return 0;
    urlBytes += url.size() * 2;
    urls.append(url);
    return urls.size() - 1;
  }
  void set(int y, int x, int url) {
    if (y >= 0 && y < rows && x >= 0 && x < cols)
      grid[screen][y * cols + x] = url;
  }
  ~State() {
    if (vt)
      vterm_free(vt);
  }
#endif
};
TerminalLinks::TerminalLinks(int rows, int cols)
    : s(std::make_unique<State>()) {
#ifdef Q_OS_MACOS
  s->rows = rows;
  s->cols = cols;
  for (auto &g : s->grid)
    g.resize(rows * cols);
  s->vt = vterm_new(rows, cols);
  vterm_set_utf8(s->vt, 1);
  static const VTermStateCallbacks cb = [] {
    VTermStateCallbacks c{};
    c.putglyph = [](VTermGlyphInfo *g, VTermPos p, void *data) {
      auto &s = *static_cast<State *>(data);
      for (int i = 0; i < g->width; ++i)
        s.set(p.row, p.col + i, s.active);
      return 1;
    };
    c.moverect = State::move;
    c.erase = State::erase;
    c.scrollrect = [](VTermRect r, int down, int right, void *d) {
      vterm_scroll_rect(r, down, right, State::move, State::erase, d);
      return 1;
    };
    c.settermprop = [](VTermProp prop, VTermValue *v, void *d) {
      auto &s = *static_cast<State *>(d);
      if (prop == VTERM_PROP_ALTSCREEN) {
        s.screen = v->boolean ? 1 : 0;
        if (s.screen)
          s.grid[1].fill(0);
      }
      return 1;
    };
    c.initpen = [](void *d) {
      static_cast<State *>(d)->active = 0;
      return 1;
    };
    return c;
  }();
  static const VTermStateFallbacks fallback = [] {
    VTermStateFallbacks f{};
    f.osc = [](int command, VTermStringFragment f, void *d) {
      if (command != 8)
        return 0;
      auto &s = *static_cast<State *>(d);
      if (f.initial) {
        s.fragment.clear();
        s.overflow = false;
        s.active = 0;
      }
      if (s.fragment.size() + f.len > 16384)
        s.overflow = true;
      if (!s.overflow)
        s.fragment.append(f.str, f.len);
      if (f.final) {
        const auto split = s.fragment.indexOf(';');
        s.active = !s.overflow && split >= 0
                       ? s.intern(safeUrl(
                             QString::fromUtf8(s.fragment.mid(split + 1))))
                       : 0;
        s.fragment.clear();
      }
      return 1;
    };
    return f;
  }();
  auto *state = vterm_obtain_state(s->vt);
  vterm_state_set_callbacks(state, &cb, s.get());
  vterm_state_set_unrecognised_fallbacks(state, &fallback, s.get());
  vterm_state_reset(state, 1);
#else
  Q_UNUSED(rows);
  Q_UNUSED(cols);
#endif
}
TerminalLinks::~TerminalLinks() = default;
void TerminalLinks::feed(const char *data, int size) {
#ifdef Q_OS_MACOS
  vterm_input_write(s->vt, data, size);
#else
  Q_UNUSED(data);
  Q_UNUSED(size);
#endif
}
void TerminalLinks::resize(int rows, int cols) {
#ifdef Q_OS_MACOS
  // 宽度改变时原屏幕会重新布局；宁可清空旧链接，不能把旧地址套在新文字上。
  s->rows = rows;
  s->cols = cols;
  for (auto &g : s->grid)
    g.fill(0, rows * cols);
  vterm_set_size(s->vt, rows, cols);
#else
  Q_UNUSED(rows);
  Q_UNUSED(cols);
#endif
}
QString TerminalLinks::at(int row, int col) const {
#ifdef Q_OS_MACOS
  return s->urls.value(s->cell(row, col));
#else
  Q_UNUSED(row);
  Q_UNUSED(col);
  return {};
#endif
}
} // namespace exagent
