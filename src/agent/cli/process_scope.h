#pragma once
#include <QHash>
#include <QTimer>
namespace exagent::cli {
// 只跟踪本次CLI的进程组及带出生时间的后代，不能按名称清理其他任务。
class ProcessScope {
public:
  explicit ProcessScope(QObject *owner);
  bool start(qint64 pid);
  void capture();
  void terminate();
  bool quiet();
  void disarm();

private:
  qint64 group = 0;
  quint64 rootBirth = 0;
  QHash<qint64, quint64> descendants;
  QTimer monitor;
};
} // namespace exagent::cli
