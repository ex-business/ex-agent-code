#pragma once
#include "codex_connection.h"
namespace exagent::codex {
// 每次派发拥有独立连接。账号目录与线程用于跨回合恢复，运行事件按 runId 路由。
class Run final : public QObject {
    Q_OBJECT
public:
    explicit Run(QJsonObject spec, QObject *parent);
    void start();
    void interrupt();
    bool decide(const QString &token, bool allow);
signals:
    void produced(QJsonObject payload);
    void ended();

private:
    void request(const QString &method, const QJsonObject &params);
    void receive(const QJsonObject &frame);
    void result(const QString &method, const QJsonObject &value);
    void notification(const QString &method, const QJsonObject &params);
    void finish(int state, const QString &detail);
    void emitEvent(QJsonObject event);
    void output(
        const QString &itemId, const QString &kind, const QString &text, bool replace, const QString &title = {});
    void flushOutput();
    void approval(const QString &method, const QJsonValue &id, const QJsonObject &params);
    QJsonObject m_spec;
    Connection m_connection;
    QTimer m_deadline, m_flush;
    QString m_thread, m_turn;
    QHash<int, QString> m_pending;
    QHash<QString, QJsonValue> m_approvals;
    QList<QJsonObject> m_output;
    int m_id = 0;
    bool m_done = false, m_stopping = false, m_dispatched = false;
};
}
