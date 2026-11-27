#pragma once
#include "sharing_events.h"
#include <QJsonArray>
#include <QJsonObject>
namespace exagent::sharing {
// 每次安全状态变更以原子文件保留审计；只读查询不会重新读取整个文件。
class AuditStore final {
public:
  AuditStore();
  bool append(const QString &session, Event event, const QString &actor,
              const QString &object, Phase phase, const QString &operation,
              QString *error);
  QJsonObject page(const QString &session, int after, int limit,
                   QString *error) const;
  int state() const { return int(m_state); }

private:
  QString m_file;
  QJsonArray m_items;
  bool m_valid = true;
  enum class State { Ready = 0, Unavailable = 1, Full = 2 };
  State m_state = State::Ready; // 不自动清除既有记录。
};
} // namespace exagent::sharing
