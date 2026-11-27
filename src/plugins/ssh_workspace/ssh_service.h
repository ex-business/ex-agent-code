#pragma once
#include "plugins/workbench_control/control_host.h"
#include <QProcess>
namespace exagent::ssh {
class Service final : public QObject {
    Q_OBJECT
  public:
    explicit Service(control::Host &host);
    ~Service() override;
    Q_INVOKABLE QVariantMap operate(const QString &action, const QVariantMap &params = {});

  private:
    QJsonObject execute(const QString &action, const QJsonObject &params, QString *error);
    bool save(const QJsonObject &data, QString *error);
    QString request(const QJsonObject &host, QJsonObject params,
                    std::function<void(const QJsonObject &, QString &)> done, QString *error);
    QJsonObject host(const QString &id) const;
    QJsonObject workspace(const QString &id) const;
    control::Host &m_host;
    QString m_file, m_bridge;
    QJsonObject m_data;
    QHash<QString, QJsonObject> m_requests;
    QStringList m_finished;
    QHash<QString, QProcess *> m_processes;
    bool m_valid = true;
};
} // namespace exagent::ssh
