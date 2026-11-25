#pragma once

#include <QFutureWatcher>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class GitActions : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(GitActions)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(QStringList staged READ staged NOTIFY changed)
public:
    enum State { Idle = 0, Running = 1, Succeeded = 2, Failed = 3 };
    Q_ENUM(State)
    enum Operation { Stage, Unstage, Review, Commit, Push };
    explicit GitActions(QObject *parent = nullptr);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    bool busy() const { return m_busy; }
    int state() const { return m_state; }
    QString notice() const { return m_notice; }
    QString detail() const { return m_detail; }
    QStringList staged() const { return m_staged; }
    Q_INVOKABLE void stageFile(const QString &branch, const QString &path, bool unstage);
    Q_INVOKABLE void reviewCommit(const QString &branch);
    Q_INVOKABLE void commit(const QString &message);
    Q_INVOKABLE void push(const QString &branch);
signals:
    void changed();
    void reviewReady();
    void committed();
    void refreshed();
private:
    void start(Operation op, const QString &branch, const QString &value = {});
    QFutureWatcher<QVariantMap> m_job;
    QString m_folder, m_branch, m_notice, m_detail;
    QByteArray m_snapshot;
    QStringList m_staged;
    quint64 m_context = 0, m_started = 0;
    Operation m_operation = Review;
    bool m_busy = false;
    State m_state = Idle;
};
}
