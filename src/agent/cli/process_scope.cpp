#include "process_scope.h"
#include <QSet>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <libproc.h>
#include <sys/proc.h>
#endif
namespace exagent::cli {
ProcessScope::ProcessScope(QObject *owner) {
  monitor.setInterval(100);
  QObject::connect(&monitor, &QTimer::timeout, owner, [this] { capture(); });
}
bool ProcessScope::start(qint64 pid) {
#ifdef Q_OS_UNIX
  if (pid <= 1 || getpgid(pid) != pid)
    return false;
  group = pid;
#ifdef Q_OS_MACOS
  proc_bsdinfo root{};
  if (proc_pidinfo(int(pid), PROC_PIDTBSDINFO, 0, &root, sizeof(root)) !=
      sizeof(root))
    return false;
  rootBirth = quint64(root.pbi_start_tvsec) * 1000000 + root.pbi_start_tvusec;
#endif
  monitor.start();
  capture();
  return true;
#else
  Q_UNUSED(pid);
  return false;
#endif
}
void ProcessScope::capture() {
#ifdef Q_OS_MACOS
  QList<qint64> parents = descendants.keys();
  if (group)
    parents.append(group);
  QSet<qint64> seen;
  while (!parents.isEmpty() && seen.size() < 4096) {
    auto parent = parents.takeLast();
    if (seen.contains(parent))
      continue;
    seen.insert(parent);
    proc_bsdinfo current{};
    const auto expected =
        parent == group ? rootBirth : descendants.value(parent);
    if (proc_pidinfo(int(parent), PROC_PIDTBSDINFO, 0, &current,
                     sizeof(current)) != sizeof(current) ||
        quint64(current.pbi_start_tvsec) * 1000000 + current.pbi_start_tvusec !=
            expected)
      continue;
    int pids[1024];
    const int count = proc_listchildpids(int(parent), pids, sizeof(pids));
    for (int i = 0; i < qMin(count, 1024); ++i) {
      proc_bsdinfo info{};
      if (pids[i] > 1 && proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &info,
                                      sizeof(info)) == sizeof(info)) {
        descendants.insert(pids[i], quint64(info.pbi_start_tvsec) * 1000000 +
                                        info.pbi_start_tvusec);
        parents.append(pids[i]);
      }
    }
  }
#endif
}
void ProcessScope::terminate() {
  capture();
#ifdef Q_OS_UNIX
  if (group > 1)
    kill(-pid_t(group), SIGKILL);
#endif
#ifdef Q_OS_MACOS
  for (auto it = descendants.cbegin(); it != descendants.cend(); ++it) {
    proc_bsdinfo info{};
    if (proc_pidinfo(int(it.key()), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) ==
            sizeof(info) &&
        quint64(info.pbi_start_tvsec) * 1000000 + info.pbi_start_tvusec ==
            it.value())
      kill(pid_t(it.key()), SIGKILL);
  }
#endif
}
void ProcessScope::disarm() {
  monitor.stop();
  group = 0;
  descendants.clear();
}
bool ProcessScope::quiet() {
#ifdef Q_OS_MACOS
  for (auto it = descendants.begin(); it != descendants.end();) {
    proc_bsdinfo info{};
    if (proc_pidinfo(int(it.key()), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) !=
            sizeof(info) ||
        quint64(info.pbi_start_tvsec) * 1000000 + info.pbi_start_tvusec !=
            it.value() ||
        info.pbi_status == SZOMB)
      it = descendants.erase(it);
    else
      ++it;
  }
  if (!descendants.isEmpty())
    return false;
#endif
  monitor.stop();
  group = 0;
  return true;
}
} // namespace exagent::cli
