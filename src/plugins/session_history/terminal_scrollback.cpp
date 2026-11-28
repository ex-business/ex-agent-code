#include "terminal_scrollback.h"
#include "history_provider.h"
#include "terminal_backend.h"
#include <QElapsedTimer>
#include <QProcess>
namespace exagent {
QVariantMap terminalScrollback(const QString &id, const QString &fallback) {
  QProcess p;
  const auto program = cliPath("tmux");
  if (program.isEmpty())
    return {{"text", fallback},
            {"error", QStringLiteral("未找到tmux，显示已保存快照。")},
            {"limited", true}};
  p.start(program, {"-L", terminalSocket(), "capture-pane", "-p", "-J", "-t",
                    "=" + id + ":0.0", "-S", "-20000"});
  if (!p.waitForStarted(1000))
    return {{"text", fallback},
            {"error", QStringLiteral("无法读取终端回滚，显示已保存快照。")},
            {"limited", true}};
  const auto meta = terminalCommand(
      {"display-message", "-p", "-t", "=" + id + ":0.0", "#{history_size}"});
  bool countKnown = false;
  const auto historyRows = meta.output.trimmed().toInt(&countKnown);
  const bool rowLimited = !meta.ok || !countKnown || historyRows >= 20000;
  constexpr int limit = 4 * 1024 * 1024;
  QByteArray text;
  bool clipped = false;
  QElapsedTimer timer;
  timer.start();
  // 边读边保留尾部，不能先把无界终端输出装进内存再截断。
  auto drain = [&] {
    text += p.readAllStandardOutput();
    if (text.size() > limit) {
      text.remove(0, text.size() - limit);
      clipped = true;
    }
    p.readAllStandardError();
  };
  while (p.state() != QProcess::NotRunning && timer.elapsed() < 5000) {
    p.waitForReadyRead(40);
    drain();
  }
  drain();
  if (p.state() != QProcess::NotRunning) {
    p.kill();
    p.waitForFinished(1000);
    return {{"text", fallback},
            {"error", QStringLiteral("读取终端回滚超时，显示已保存快照。")},
            {"limited", true}};
  }
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
    return {{"text", fallback},
            {"error", QStringLiteral("终端回滚暂不可用，显示已保存快照。")},
            {"limited", true}};
  if (clipped) {
    const auto newline = text.indexOf('\n');
    if (newline >= 0)
      text.remove(0, newline + 1);
    else {
      while (!text.isEmpty() && (uchar(text[0]) & 0xc0) == 0x80)
        text.remove(0, 1);
    }
  }
  return {{"text", QString::fromUtf8(text)},
          {"limited", clipped || rowLimited},
          {"error", QString()},
          {"maxRows", 20000}};
}
} // namespace exagent
