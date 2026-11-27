#pragma once
#include "http_server.h"
#include "plugins/workbench_control/control_host.h"
namespace exagent::mobile {
class Service final : public QObject {
    Q_OBJECT
  public:
    explicit Service(control::Host &host);
    std::function<bool(const QString &, int, const QString &, const QString &, int, const QString &, QString *)> auditShared;
    Q_INVOKABLE void setAppearance(const QVariantMap &colors, const QVariantMap &strings,
                                   const QString &language);
    Q_INVOKABLE QVariantMap operate(const QString &action, const QVariantMap &params = {});

  private:
    QJsonObject execute(const QString &, const QJsonObject &, QString *);
    Response serve(const Request &);
    bool audit(const QString &, int, const QString &, const QString &, int, const QString &, QString *);
    bool save(const QJsonObject &, QString *);
    control::Host &m_host;
    HttpServer m_server;
    QString m_file;
    QJsonObject m_data;
    QJsonObject m_appearance;
    QHash<QString, QJsonObject> m_offers;
    bool m_valid = true;
};
} // namespace exagent::mobile
