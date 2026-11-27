#pragma once
#include "codex_connection.h"
#include "thread_input.h"
#include "agent/runtime_run.h"
namespace exagent::codex {
// 每次派发拥有独立连接。账号目录与线程用于跨回合恢复，运行事件按 runId 路由。
class Run final : public agent::RuntimeRun {
    Q_OBJECT
public:
    explicit Run(QJsonObject spec, QObject *parent);
    void start();
    bool ownsSession(const QString &session) const { return m_spec["conversationId"] == session; }
    void interrupt();
    bool steer(const QString &id, const QString &turn, const QString &text);
    bool decide(const QString &token, bool allow);

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
    std::unique_ptr<ThreadInputLock> m_input;
    void report(int status);
    QVariantMap m_activity;
    Connection m_connection;
    QTimer m_deadline, m_flush;
    QString m_thread, m_turn;
    QHash<int, QString> m_pending;
    QHash<int, QString> m_mail;
    QHash<QString, QJsonValue> m_approvals;
    QList<QJsonObject> m_output;
    int m_id = 0;
    bool m_done = false, m_stopping = false, m_dispatched = false;
};
}
