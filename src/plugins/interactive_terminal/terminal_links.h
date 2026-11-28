#pragma once
#include <QString>
#include <memory>
namespace exagent {
// 仅跟踪可见单元格的OSC 8元数据，不保存终端正文或执行链接。
class TerminalLinks {
public:
  TerminalLinks(int rows, int cols);
  ~TerminalLinks();
  void feed(const char *data, int size);
  void resize(int rows, int cols);
  QString at(int row, int col) const;
  static QString safeUrl(const QString &text);

private:
  struct State;
  std::unique_ptr<State> s;
};
} // namespace exagent
