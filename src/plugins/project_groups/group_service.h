#pragma once
#include "plugins/workbench_control/control_host.h"
#include <QFutureWatcher>
#include <atomic>
namespace exagent::groups {
class Service final : public QObject {
  Q_OBJECT
public:
  explicit Service(control::Host &host);
  ~Service() override;
  Q_INVOKABLE QVariantMap operate(const QString &action,
                                  const QVariantMap &params = {});

private:
  enum class JobState { Working = 0, Ready = 1, Failed = 2, Cancelled = 3 };
  struct Job {
    JobState state = JobState::Working;
    QString root;
    QVariantMap result;
    std::shared_ptr<std::atomic_bool> cancel;
  };
  QJsonObject execute(const QString &, const QJsonObject &, QString *);
  bool publish(const ProjectCatalog &next, QString *error);
  control::Host &m_host;
  QHash<QString, Job> m_jobs;
  QStringList m_jobOrder;
  QSet<QString> m_inFlight;
};
} // namespace exagent::groups
