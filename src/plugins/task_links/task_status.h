#pragma once
#include "task_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class TaskStatus : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskStatus)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    using QObject::QObject;
    bool busy() const { return !m_reply.isNull(); }
    QString title() const { return m_snapshot.title; }
    QString status() const { return m_snapshot.status; }
    QString error() const { return m_error; }
    Q_INVOKABLE QVariantList taskRows(const QVariantList &workspaces) const;
    Q_INVOKABLE QVariantMap describe(const QString &url) const;
    Q_INVOKABLE void refresh(const QString &url);
    Q_INVOKABLE void reset();
    Q_INVOKABLE bool openSource(const QString &url) const;
signals:
    void changed();
private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    tasklinks::Snapshot m_snapshot;
    QString m_error;
};
}
