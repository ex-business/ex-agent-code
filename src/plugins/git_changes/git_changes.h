#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

namespace exagent {
class GitChanges : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(GitChanges)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QStringList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(QString selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    enum Area { Worktree = 1, Index = 2, Untracked = 3, Conflict = 4 };
    Q_ENUM(Area)
    explicit GitChanges(QObject *parent = nullptr);
    ~GitChanges() override;
    QString folder() const { return m_folder; }
    QVariantList entries() const { return m_entries; }
    QStringList lines() const { return m_lines; }
    QString selection() const { return m_key; }
    QString message() const { return m_message; }
    void setFolder(const QString &folder);
    Q_INVOKABLE void select(const QString &key);
    Q_INVOKABLE void refresh();

signals:
    void folderChanged();
    void entriesChanged();
    void linesChanged();
    void selectionChanged();
    void messageChanged();

private:
    void invalidate();
    void publishLines(const QStringList &lines);
    void publishMessage(const QString &message);
    QString m_folder, m_key, m_message;
    QVariantList m_entries;
    QStringList m_lines;
    QFutureWatcher<QVariantMap> m_job;
    QTimer m_poll;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
