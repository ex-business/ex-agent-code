#pragma once
#include "agent/runtime_run.h"
#include <QQueue>
#include <functional>
namespace exagent::agent {
class Worker final : public QObject {
  Q_OBJECT
public:
  using Factory = std::function<RuntimeRun *(QJsonObject, QObject *)>;
  explicit Worker(QObject *parent = nullptr, QHash<int, Factory> adapters = {});
  void registerAdapter(int account, Factory factory);
  bool command(const QString &method, const QJsonObject &params,
               QString *error);
  QQueue<QJsonObject> events;
  qsizetype queuedBytes = 0;
signals:
  void available();

private:
  QHash<QString, RuntimeRun *> m_runs;
  QHash<int, Factory> m_factories;
  QSet<QString> m_sessions;
};
} // namespace exagent::agent
