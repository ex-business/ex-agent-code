#pragma once
#include "plugins/workbench_control/control_host.h"
#include <QJsonArray>
namespace exagent::plugins {
class Manager final : public QObject {
    Q_OBJECT
  public:
    explicit Manager(control::Host &host);
    Q_INVOKABLE QVariantMap operate(const QString &action, const QVariantMap &params = {});
  signals:
    void changed();

  private:
    QJsonObject execute(const QString &action, const QJsonObject &params, QString *error);
    bool commit(const QJsonArray &entries, QString *error);
    QJsonObject inspect(const QString &directory, QString *error) const;
    control::Host &m_host;
    QString m_file;
    QJsonArray m_entries;
    bool m_valid = true;
};
} // namespace exagent::plugins
