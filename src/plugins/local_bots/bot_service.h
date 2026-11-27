#pragma once
#include "plugins/workbench_control/control_host.h"
namespace exagent::bots {
enum State { Preparing = 0, Active = 1, Paused = 2 };
class Service final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
  public:
    explicit Service(control::Host &host);
    QVariantMap current() const;
  signals:
    void changed();

  public:
    Q_INVOKABLE QVariantMap operate(const QString &action, const QVariantMap &params = {});

  private:
    QJsonObject execute(const QString &action, const QJsonObject &params, QString *error);
    bool commit(const QJsonArray &bots, QString *error);
    QJsonObject prepare(const QString &id, QString *error);
    bool policy(const ChatSession &session, int channel, QString &text, QString *error) const;
    bool busy(const QJsonObject &bot) const;
    control::Host &m_host;
    QString m_file;
    QJsonArray m_bots;
    bool m_valid = true;
};
} // namespace exagent::bots
