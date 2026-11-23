#pragma once
#include "codex_connection.h"
#include <QSet>
namespace exagent::codex {
class ModelCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ModelCatalog(Account account, QObject *parent);
    void start();
signals:
    void loaded(QStringList models);
    void failed(QString reason);

private:
    Connection m_connection;
    QTimer m_timeout;
    Account m_account;
    QStringList m_models;
    QSet<QString> m_cursors;
    int m_id = 0;
    void page(const QString &cursor);
};
}
