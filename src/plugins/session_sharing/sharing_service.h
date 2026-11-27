#pragma once
#include "audit_store.h"
#include "conversation_feed.h"
#include "plugins/workbench_control/control_host.h"
#include <QElapsedTimer>
namespace exagent::sharing {
// 控制租约仅属于当前桌面运行期，重启后必须由桌面重新交接。
class Service final : public QObject {
public:
  explicit Service(control::Host &host);

private:
  struct Lease {
    QString device;
    qint64 deadline = 0;
    int revision = 0;
    QSet<QString> requests;
  };
  QJsonObject execute(const QString &, const QJsonObject &, QString *);
  QJsonObject device(const QString &) const;
  Lease &lease(const QString &);
  bool eligible(const QJsonObject &, const QString &) const;
  QJsonObject view(const QString &);
  control::Host &m_host;
  AuditStore m_audit;
  ConversationFeed m_feed;
  QHash<QString, Lease> m_leases;
  QElapsedTimer m_clock;
  QString m_epoch, m_dispatchSession, m_dispatchDevice;
};
} // namespace exagent::sharing
