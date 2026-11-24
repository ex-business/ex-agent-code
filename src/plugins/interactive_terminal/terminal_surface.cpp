#include "terminal_surface.h"
#include "plugins/session_history/history_provider.h"
#include "plugins/session_history/terminal_backend.h"
#include <QClipboard>
#include <QCursor>
#include <QUrl>
#include <QImage>
#include <QDesktopServices>
#include <QMimeData>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QHoverEvent>
#include <QDragEnterEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPainter>
#include <QProcess>
#include <QSocketNotifier>
#include <QUuid>
#ifdef Q_OS_MACOS
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <util.h>
#include <vterm.h>
#endif

namespace exagent {
struct TerminalSurface::Client {
#ifdef Q_OS_MACOS
  VTerm *vt = nullptr;
  VTermScreen *screen = nullptr;
  int master = -1;
  QProcess process;
  std::unique_ptr<QSocketNotifier> reader, writer;
  QByteArray pending;
  int rows = 1, cols = 1;
  bool cursorVisible = true;
  QByteArray selection, selectionBuffer = QByteArray(65536, '\0');
  bool selectionOverflow = false;
  ~Client() {
    reader.reset();
    writer.reset();
    // 只关闭附接客户端的 PTY；tmux 服务仍然保留窗格中的进程。
    if (master >= 0)
      close(master);
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(1000);
    }
    if (vt)
      vterm_free(vt);
  }
  void flush() {
    while (!pending.isEmpty()) {
      const auto n = write(master, pending.constData(), pending.size());
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
          pending.clear();
        break;
      }
      pending.remove(0, n);
    }
    writer->setEnabled(!pending.isEmpty());
  }
#endif
};

TerminalSurface::TerminalSurface(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
  setFlag(ItemAcceptsInputMethod, true);
  setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
  setAcceptHoverEvents(true);
  setFlag(ItemAcceptsDrops, true);
  m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  // Powerline 提示符使用私有区字形，优先选择本机已安装的终端等宽字体。
  const auto families = QFontDatabase::families();
  for (const auto& family : {QStringLiteral("MesloLGS NF"), QStringLiteral("FiraCode Nerd Font Mono")}) {
    if (families.contains(family)) { m_font.setFamily(family); break; }
  }
  m_font.setPixelSize(14);
  const QFontMetrics fm(m_font);
  m_cw = qMax(1, fm.horizontalAdvance('M'));
  m_ch = fm.height();
  m_ascent = fm.ascent();
}
TerminalSurface::~TerminalSurface() {
#ifdef Q_OS_MACOS
  if (m_client)
    m_client->process.disconnect(this);
#endif
}
bool TerminalSurface::supported() const {
#ifdef Q_OS_MACOS
  return true;
#else
  return false;
#endif
}
void TerminalSurface::fail(const QString &text) {
  m_error = text;
  emit errorChanged();
}
void TerminalSurface::setTarget(const QString &id) {
  if (m_target == id)
    return;
#ifdef Q_OS_MACOS
  if (m_client)
    m_client->process.disconnect(this);
#endif
  m_client.reset();
  m_target = id;
  m_preedit.clear();
  m_selectionStart = m_selectionEnd = QPoint(-1,-1);
  m_contextLink.clear(); emit contextLinkChanged();
  fail({});
  emit targetChanged();
  update();
#ifdef Q_OS_MACOS
  if (id.isEmpty())
    return;
  if (!id.startsWith("ex-") || QUuid(id.mid(3)).isNull()) {
    fail(QStringLiteral("无效的终端标识。"));
    return;
  }
  const auto exe = cliPath("tmux");
  if (exe.isEmpty()) {
    fail(QStringLiteral("未找到 tmux。"));
    return;
  }
  m_client = std::make_unique<Client>();
  auto &c = *m_client;
  int slave = -1;
  winsize ws{};
  ws.ws_col = qBound(2, int(width() / m_cw), 512);
  ws.ws_row = qBound(1, int(height() / m_ch), 256);
  if (openpty(&c.master, &slave, nullptr, nullptr, &ws) < 0) {
    fail(QStringLiteral("无法创建交互终端。"));
    return;
  }
  fcntl(c.master, F_SETFD, FD_CLOEXEC);
  fcntl(slave, F_SETFD, FD_CLOEXEC);
  fcntl(c.master, F_SETFL, O_NONBLOCK);
  c.cols = ws.ws_col;
  c.rows = ws.ws_row;
  c.vt = vterm_new(c.rows, c.cols);
  vterm_set_utf8(c.vt, 1);
  c.screen = vterm_obtain_screen(c.vt);
  static const VTermScreenCallbacks callbacks = [] {
    VTermScreenCallbacks cb{};
    cb.movecursor = [](VTermPos, VTermPos, int visible, void *data) {
      static_cast<Client *>(data)->cursorVisible = visible;
      return 1;
    };
    cb.settermprop = [](VTermProp prop, VTermValue *value, void *data) {
      if (prop == VTERM_PROP_CURSORVISIBLE)
        static_cast<Client *>(data)->cursorVisible = value->boolean;
      return 1;
    };
    return cb;
  }();
  vterm_screen_set_callbacks(c.screen, &callbacks, &c);
  vterm_screen_enable_altscreen(c.screen, 1);
  vterm_screen_reset(c.screen, 1);
  static const VTermSelectionCallbacks selectionCallbacks = {
      [](VTermSelectionMask, VTermStringFragment fragment, void *data) {
        auto *surface = static_cast<TerminalSurface *>(data);
        auto &client = *surface->m_client;
        if (fragment.initial) { client.selection.clear(); client.selectionOverflow = false; }
        if (client.selection.size() + fragment.len > 1024 * 1024) client.selectionOverflow = true;
        if (!client.selectionOverflow) client.selection.append(fragment.str, fragment.len);
        // OSC 52 仅接受远端写入，查询不回传本机剪贴板。
        if (fragment.final && !client.selectionOverflow)
          QGuiApplication::clipboard()->setText(QString::fromUtf8(client.selection));
        return 1;
      },
      [](VTermSelectionMask, void *) { return 1; }
  };
  vterm_state_set_selection_callbacks(vterm_obtain_state(c.vt), &selectionCallbacks, this,
                                    c.selectionBuffer.data(), c.selectionBuffer.size());
  applyPalette();
  c.writer =
      std::make_unique<QSocketNotifier>(c.master, QSocketNotifier::Write);
  c.writer->setEnabled(false);
  connect(c.writer.get(), &QSocketNotifier::activated, this,
          [&c] { c.flush(); });
  vterm_output_set_callback(
      c.vt,
      [](const char *data, size_t size, void *ctx) {
        auto *client = static_cast<Client *>(ctx);
        client->pending.append(data, size);
        client->writer->setEnabled(true);
      },
      &c);
  c.reader = std::make_unique<QSocketNotifier>(c.master, QSocketNotifier::Read);
  connect(c.reader.get(), &QSocketNotifier::activated, this, [this, &c] {
    char data[16384];
    // 每轮设定读取上限，快速输出时给输入和窗口布局保留事件循环时间。
    for (int i = 0; i < 8; ++i) {
      auto n = read(c.master, data, sizeof(data));
      if (n > 0) {
        vterm_input_write(c.vt, data, n);
        continue;
      }
      if (n == 0 || (errno != EAGAIN && errno != EINTR))
        c.reader->setEnabled(false);
      break;
    }
    vterm_screen_flush_damage(c.screen);
    update();
  });
  auto env = QProcessEnvironment::systemEnvironment();
  env.remove("TMUX");
  env.insert("TERM", "xterm-256color");
  c.process.setProcessEnvironment(env);
  c.process.setProcessChannelMode(QProcess::ForwardedChannels);
  c.process.setInputChannelMode(QProcess::ForwardedInputChannel);
  c.process.setChildProcessModifier([slave] {
    // tmux 必须使用自己的控制终端，键盘信号和窗口尺寸才会送达当前 TUI。
    if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0)
      _exit(126);
    for (int fd = 0; fd < 3; ++fd)
      if (dup2(slave, fd) < 0)
        _exit(126);
  });
  connect(&c.process, &QProcess::finished, this,
          [this] { fail(QStringLiteral("交互连接已结束，可返回记录视图。")); });
  c.process.start(exe, {"-L", terminalSocket(), "-f", "/dev/null",
                        "set-option", "-t", "=" + id + ":0", "status", "off", ";",
                        "set-option", "-t", "=" + id, "set-clipboard", "on", ";",
                        "attach-session", "-t", "=" + id});
  const bool started = c.process.waitForStarted(1500);
  close(slave);
  if (!started)
    fail(QStringLiteral("无法附接 tmux 客户端。"));
#endif
}
void TerminalSurface::geometryChange(const QRectF &next, const QRectF &old) {
  QQuickPaintedItem::geometryChange(next, old);
  resizeGrid();
}
void TerminalSurface::resizeGrid() {
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->vt)
    return;
  auto &c = *m_client;
  winsize ws{};
  ws.ws_col = qBound(2, int(width() / m_cw), 512);
  ws.ws_row = qBound(1, int(height() / m_ch), 256);
  if (ws.ws_col == c.cols && ws.ws_row == c.rows)
    return;
  c.cols = ws.ws_col;
  c.rows = ws.ws_row;
  vterm_set_size(c.vt, c.rows, c.cols);
  ioctl(c.master, TIOCSWINSZ, &ws);
  update();
#endif
}
QColor TerminalSurface::themeColor(const QString &role, const QColor &fallback) const {
  const QColor color(m_theme.value(role).toString());
  return color.isValid() ? color : fallback;
}
void TerminalSurface::setTheme(const QVariantMap &theme) {
  if (m_theme==theme) return;
  m_theme=theme; applyPalette(); update(); emit themeChanged();
}
void TerminalSurface::applyPalette() {
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->vt) return;
  const QColor defaults[] = {"#232932","#dd8283","#8fbb9d","#d1b887","#879ec4","#b49cc8","#85b5bc","#c8cfd8", "#75818e","#eea1a1","#aad6b6","#ead09c","#a7bfe5","#ceb5e1","#a1d1d6","#f1f4f7"};
  auto *state=vterm_obtain_state(m_client->vt);
  // 重置整张索引色表后覆盖导入项，避免切换主题残留上一套颜色。
  for (int i=0;i<256;++i) {
    QColor fallback;
    if (i<16) fallback=defaults[i];
    else if (i<232) {
      const auto component=[](int v){return v==0 ? 0 : 55+40*v;};
      const int index=i-16;
      fallback=QColor(component(index/36),component(index/6%6),component(index%6));
    } else fallback=QColor(8+(i-232)*10,8+(i-232)*10,8+(i-232)*10);
    const auto color=themeColor("ansi"+QString::number(i),fallback);
    VTermColor terminalColor;
    vterm_color_rgb(&terminalColor,color.red(),color.green(),color.blue());
    vterm_state_set_palette_color(state,i,&terminalColor);
  }
#endif
}
void TerminalSurface::sendControl(int action) {
#ifdef Q_OS_MACOS
  if (!m_client || !m_error.isEmpty()) return;
  static const char keys[]={'c','d','l','a','e'};
  if (action<0 || action>=5) return;
  vterm_keyboard_unichar(m_client->vt,keys[action],VTERM_MOD_CTRL);
  m_client->flush();
#else
  Q_UNUSED(action)
#endif
}
void TerminalSurface::paint(QPainter *p) {
  p->fillRect(boundingRect(), themeColor("background",QColor("#191d24")));
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->screen)
    return;
  auto &c = *m_client;
  for (int y = 0; y < c.rows; ++y)
    for (int x = 0; x < c.cols; ++x) {
      VTermScreenCell cell{};
      if (!vterm_screen_get_cell(c.screen, {y, x}, &cell) ||
          cell.chars[0] == UINT32_MAX)
        continue;
      auto rgb = [&](VTermColor color) {
        vterm_screen_convert_color_to_rgb(c.screen, &color);
        return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
      };
      QColor fg = VTERM_COLOR_IS_DEFAULT_FG(&cell.fg) ? themeColor("foreground",QColor("#edeef0"))
                                                      : rgb(cell.fg);
      QColor bg = VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) ? themeColor("background",QColor("#191d24"))
                                                      : rgb(cell.bg);
      if (cell.attrs.reverse)
        std::swap(fg, bg);
      const int start = m_selectionStart.y()*c.cols+m_selectionStart.x();
      const int end = m_selectionEnd.y()*c.cols+m_selectionEnd.x();
      const int index = y*c.cols+x;
      if (m_selectionStart.x() >= 0 && index >= qMin(start,end) && index < qMax(start,end)) bg = themeColor("selection-background",QColor("#3b526e"));
      if (bg != themeColor("background",QColor("#191d24")))
        p->fillRect(x * m_cw, y * m_ch, m_cw * qMax(1, int(cell.width)), m_ch,
                    bg);
      if (!cell.chars[0])
        continue;
      QString text;
      for (char32_t ch : cell.chars) {
        if (!ch)
          break;
        text += QString::fromUcs4(&ch, 1);
      }
      auto font = m_font;
      font.setBold(cell.attrs.bold);
      font.setUnderline(cell.attrs.underline);
      font.setItalic(cell.attrs.italic);
      p->setFont(font);
      p->setPen(fg);
      p->drawText(x * m_cw, y * m_ch + m_ascent, text);
    }
  VTermPos pos;
  vterm_state_get_cursorpos(vterm_obtain_state(c.vt), &pos);
  if (hasActiveFocus() && c.cursorVisible) {
    p->setPen(themeColor("cursor-color",QColor(Qt::white)));
    p->drawLine(pos.col * m_cw, pos.row * m_ch, pos.col * m_cw,
                (pos.row + 1) * m_ch - 1);
  }
  if (!m_preedit.isEmpty()) {
    p->setPen(themeColor("cursor-color",QColor(Qt::white)));
    p->drawText(pos.col * m_cw, pos.row * m_ch + m_ascent, m_preedit);
  }
#endif
}
QPoint TerminalSurface::cellAt(const QPointF &position) const {
  return {qBound(0,int(position.x()/m_cw),qMax(0,int(width()/m_cw))),
          qBound(0,int(position.y()/m_ch),qMax(0,int(height()/m_ch)-1))};
}
QString TerminalSurface::lineText(int row, int start, int end) const {
  QString result;
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->screen) return result;
  for (int col=start; col<(end<0 ? m_client->cols : qMin(end,m_client->cols)); ++col) {
    VTermScreenCell cell{};
    if (!vterm_screen_get_cell(m_client->screen,{row,col},&cell)) break;
    if (cell.chars[0] == UINT32_MAX) continue;
    if (!cell.chars[0]) result += ' ';
    for (char32_t ch : cell.chars) { if (!ch) break; result += QString::fromUcs4(&ch,1); }
  }
#endif
  return result;
}
QString TerminalSurface::linkAt(const QPointF &position) const {
  const auto cell = cellAt(position);
  const auto line = lineText(cell.y());
  // 只识别可公开打开的网页链接，避免终端输出触发自定义协议执行。
  static const QRegularExpression pattern(QStringLiteral(R"(https?://[^\s<>"']+)"));
  auto matches = pattern.globalMatch(line);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto left = QFontMetrics(m_font).horizontalAdvance(line.left(match.capturedStart()));
    const auto right = QFontMetrics(m_font).horizontalAdvance(line.left(match.capturedEnd()));
    if (position.x() >= left && position.x() < right) return match.captured();
  }
  return {};
}
void TerminalSurface::hoverMoveEvent(QHoverEvent *event) {
  setCursor(linkAt(event->position()).isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
}
void TerminalSurface::mousePressEvent(QMouseEvent *event) {
  forceActiveFocus();
  m_contextLink = linkAt(event->position()); emit contextLinkChanged();
  if (event->button() == Qt::RightButton) emit contextMenuRequested(event->position().x(),event->position().y());
  else if ((event->modifiers() & Qt::ControlModifier) && !m_contextLink.isEmpty()) openLink();
  else m_selectionStart = m_selectionEnd = cellAt(event->position());
  event->accept(); update();
}
void TerminalSurface::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) { m_selectionEnd = cellAt(event->position()); update(); }
  event->accept();
}
void TerminalSurface::mouseReleaseEvent(QMouseEvent *event) { event->accept(); }
void TerminalSurface::copySelection() {
  if (m_selectionStart.x()<0 || m_selectionStart==m_selectionEnd) return;
  auto first=m_selectionStart, last=m_selectionEnd;
  if (first.y()>last.y() || (first.y()==last.y() && first.x()>last.x())) std::swap(first,last);
  QStringList lines;
  for (int row=first.y(); row<=last.y(); ++row) {
    // 选区按终端单元格截取，避免中文宽字符和 emoji 的 UTF-16 偏移混用。
    lines.append(lineText(row,row==first.y() ? first.x() : 0,row==last.y() ? last.x() : -1));
  }
  QGuiApplication::clipboard()->setText(lines.join('\n'));
}
void TerminalSurface::copyLink() { if (!m_contextLink.isEmpty()) QGuiApplication::clipboard()->setText(m_contextLink); }
void TerminalSurface::openLink() {
  const QUrl url(m_contextLink);
  if (url.isValid() && (url.scheme()=="http" || url.scheme()=="https")) QDesktopServices::openUrl(url);
}
void TerminalSurface::insertFiles(const QList<QUrl> &urls) {
  QStringList paths;
  for (const auto &url : urls) {
    if (!url.isLocalFile()) { emit pasteRejected(QStringLiteral("仅支持拖入本机文件。")); return; }
    QString path=url.toLocalFile();
    path.replace("'", "'\"'\"'"); paths.append("'"+path+"'");
  }
  pasteText(paths.join(' '));
}
void TerminalSurface::pasteClipboard() {
  insertMimeData(QGuiApplication::clipboard()->mimeData());
}
void TerminalSurface::insertMimeData(const QMimeData *mime) {
  if (!mime || m_target.isEmpty() || !m_error.isEmpty()) return;
  if (mime->hasUrls()) { insertFiles(mime->urls()); return; }
  if (mime->hasImage()) {
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/terminal-assets/"+m_target;
    const auto path=directory+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";
    QSaveFile file(path);
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) ||
        !qvariant_cast<QImage>(mime->imageData()).save(&file,"PNG") || !file.commit()) {
      emit pasteRejected(QStringLiteral("无法保存终端图片。")); return;
    }
    insertFiles({QUrl::fromLocalFile(path)}); return;
  }
  pasteText(mime->text());
}
void TerminalSurface::dragEnterEvent(QDragEnterEvent *event) {
  if (!(event->possibleActions() & Qt::CopyAction)) return;
  if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}
void TerminalSurface::dropEvent(QDropEvent *event) {
  if (!(event->possibleActions() & Qt::CopyAction)) return;
  if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
    // 这里只接收路径或图片副本，不能向拖放来源报告移动成功。
    insertMimeData(event->mimeData());
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}
void TerminalSurface::pasteText(QString text) {
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->vt || !m_error.isEmpty()) return;
  auto &c=*m_client;
  text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
  for (const auto ch:text) {
    if (ch.category()==QChar::Other_Control && ch!='\t' && ch!='\n') {
      emit pasteRejected(QStringLiteral("剪贴板包含终端控制字符，已取消粘贴。")); return;
    }
  }
  const auto bytes=text.toUtf8();
  if (c.pending.size()+bytes.size()+12>1024*1024) {
    emit pasteRejected(QStringLiteral("待输入内容超过 1 MiB，请分段粘贴。")); return;
  }
  vterm_keyboard_start_paste(c.vt); c.pending+=bytes; vterm_keyboard_end_paste(c.vt); c.flush();
#else
  Q_UNUSED(text)
#endif
}
void TerminalSurface::keyPressEvent(QKeyEvent *e) {
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->vt || !m_error.isEmpty())
    return;
  auto &c = *m_client;
  if (e->matches(QKeySequence::Copy)) { copySelection(); e->accept(); return; }
  if (e->matches(QKeySequence::Paste)) { pasteClipboard(); e->accept(); return; }
  // macOS 的 Command/Option 编辑键沿用原生终端习惯，普通 Control 仍传给 TUI。
  if ((e->modifiers() & Qt::ControlModifier) && e->key()==Qt::Key_Left) { sendControl(3); e->accept(); return; }
  if ((e->modifiers() & Qt::ControlModifier) && e->key()==Qt::Key_Right) { sendControl(4); e->accept(); return; }
  if ((e->modifiers() & Qt::AltModifier) && (e->key()==Qt::Key_Left || e->key()==Qt::Key_Right)) {
    vterm_keyboard_unichar(c.vt,e->key()==Qt::Key_Left ? 'b' : 'f',VTERM_MOD_ALT);
    e->accept(); return;
  }
  // Qt 在 macOS 默认将物理 Control 映射为 Meta，Command 映射为 Control。
  int mod = (e->modifiers() & Qt::MetaModifier ? VTERM_MOD_CTRL : 0) |
            (e->modifiers() & Qt::AltModifier ? VTERM_MOD_ALT : 0);
  if (e->modifiers() & Qt::ControlModifier) {
    e->ignore();
    return;
  }
  static const QHash<int, VTermKey> keys = {
      {Qt::Key_Return, VTERM_KEY_ENTER},
      {Qt::Key_Enter, VTERM_KEY_ENTER},
      {Qt::Key_Tab, VTERM_KEY_TAB},
      {Qt::Key_Backspace, VTERM_KEY_BACKSPACE},
      {Qt::Key_Escape, VTERM_KEY_ESCAPE},
      {Qt::Key_Left, VTERM_KEY_LEFT},
      {Qt::Key_Right, VTERM_KEY_RIGHT},
      {Qt::Key_Up, VTERM_KEY_UP},
      {Qt::Key_Down, VTERM_KEY_DOWN},
      {Qt::Key_Delete, VTERM_KEY_DEL},
      {Qt::Key_Home, VTERM_KEY_HOME},
      {Qt::Key_End, VTERM_KEY_END},
      {Qt::Key_PageUp, VTERM_KEY_PAGEUP},
      {Qt::Key_PageDown, VTERM_KEY_PAGEDOWN}};
  auto key = keys.value(e->key(), VTERM_KEY_NONE);
  if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F35)
    key = VTermKey(VTERM_KEY_FUNCTION(1 + e->key() - Qt::Key_F1));
  if (key != VTERM_KEY_NONE)
    vterm_keyboard_key(
        c.vt, key,
        VTermModifier(
            mod | (e->modifiers() & Qt::ShiftModifier ? VTERM_MOD_SHIFT : 0)));
  else if ((mod & VTERM_MOD_CTRL) && e->key() >= Qt::Key_A &&
           e->key() <= Qt::Key_Z)
    vterm_keyboard_unichar(c.vt, e->key() - Qt::Key_A + 'a',
                           VTermModifier(mod));
  else
    for (auto ch : e->text().toUcs4())
      vterm_keyboard_unichar(c.vt, ch, VTermModifier(mod));
  e->accept();
#else
  e->ignore();
#endif
}
void TerminalSurface::inputMethodEvent(QInputMethodEvent *e) {
#ifdef Q_OS_MACOS
  if (!m_client || !m_client->vt)
    return;
  for (auto ch : e->commitString().toUcs4())
    vterm_keyboard_unichar(m_client->vt, ch, VTERM_MOD_NONE);
  m_preedit = e->preeditString();
  update();
  e->accept();
#else
  e->ignore();
#endif
}
QVariant TerminalSurface::inputMethodQuery(Qt::InputMethodQuery query) const {
#ifdef Q_OS_MACOS
  if (query == Qt::ImCursorRectangle && m_client && m_client->vt) {
    VTermPos pos;
    vterm_state_get_cursorpos(vterm_obtain_state(m_client->vt), &pos);
    return QRectF(pos.col * m_cw, pos.row * m_ch, m_cw, m_ch);
  }
#endif
  return QQuickPaintedItem::inputMethodQuery(query);
}
} // namespace exagent
