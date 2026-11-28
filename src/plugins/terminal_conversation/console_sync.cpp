#include "plugins/session_history/session_console.h"
#include "plugins/session_history/terminal_backend.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
namespace exagent {
void SessionConsole::refreshConversationTabs() {
  if (!ownsInput() || m_syncPolling)
    return;
  QVariantList bound;
  for (const auto &v : m_tabs) {
    const auto row = v.toMap();
    if (!row.value("conversationBinding").toMap().isEmpty() &&
        !m_notifiedHistory.contains(row.value("id").toString()))
      bound.append(row);
  }
  if (bound.isEmpty())
    return;
  m_syncPolling = true;
  auto *watcher = new QFutureWatcher<QVariantList>(this);
  connect(
      watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        const auto observed = watcher->result();
        watcher->deleteLater();
        m_syncPolling = false;
        // 隐藏窗口和后台页签也会回填；端点不可用时不把未知状态当退出。
        for (const auto &entry : observed) {
          const auto item = entry.toMap();
          const auto id = item.value("id").toString();
          for (const auto &v : m_tabs) {
            const auto row = v.toMap();
            if (row.value("id") == id) {
              if (item.value("state").toInt() == Ended)
                m_notifiedHistory.insert(id);
              emit conversationReady(row.value("conversationBinding").toMap());
              break;
            }
          }
        }
      });
  watcher->setFuture(QtConcurrent::run([bound] {
    QVariantList result;
    for (const auto &v : bound) {
      const auto id = v.toMap().value("id").toString();
      const auto snapshot = terminalSnapshot(id, false);
      if (snapshot.value("observed").toInt() != 1)
        continue;
      const auto state = snapshot.value("state", -1).toInt();
      if (state == Ended || state == Running)
        result.append(QVariantMap{{"id", id}, {"state", state}});
    }
    return result;
  }));
}
} // namespace exagent
