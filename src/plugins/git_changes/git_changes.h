#pragma once

#include "plugins/diff_review/patch_lines.h"
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
    Q_PROPERTY(QVariantMap fileEvidence READ fileEvidence NOTIFY linesChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY branchChanged)
    Q_PROPERTY(int scope READ scope NOTIFY scopeChanged)
    Q_PROPERTY(QString selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    enum Area { Worktree = 1, Index = 2, Untracked = 3, Conflict = 4, Compared = 5 };
    Q_ENUM(Area)
    enum Scope { Local = 0, RevisionToWorktree = 1, Revisions = 2 };
    Q_ENUM(Scope)
    explicit GitChanges(QObject *parent = nullptr);
    ~GitChanges() override;
    QString folder() const { return m_folder; }
    QVariantList entries() const { return m_entries; }
    QStringList lines() const { return m_lines; }
    QString selection() const { return m_key; }
    QString branch() const { return m_branch; }
    int scope() const { return m_scope; }
    QString message() const { return m_message; }
    void setFolder(const QString &folder);
    Q_INVOKABLE void setRange(int scope, const QString &from, const QString &to);
    Q_INVOKABLE void selectFile(const QString &path);
    Q_INVOKABLE void select(const QString &key);
    QVariantMap fileEvidence() const;
    Q_INVOKABLE QVariantMap anchorAt(int index) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshAfterWrite() { invalidate(); }

signals:
    void branchChanged();
    void scopeChanged();
    void folderChanged();
    void entriesChanged();
    void linesChanged();
    void selectionChanged();
    void messageChanged();

private:
    void invalidate();
    void publishLines(const QStringList &lines);
    void publishMessage(const QString &message);
    int m_scope = Local;
    QString m_from, m_to, m_requestedPath;
    QString m_branch, m_folder, m_key, m_message;
    QString m_fingerprint;
    QVariantMap m_version;
    review::PatchLines m_patch;
    QVariantList m_entries;
    QStringList m_lines;
    QFutureWatcher<QVariantMap> m_job;
    QTimer m_poll;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
