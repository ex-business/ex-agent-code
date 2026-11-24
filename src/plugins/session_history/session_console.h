#pragma once
#include <QHash>
#include <QLockFile>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
namespace exagent {
class SessionConsole : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList history MEMBER m_history NOTIFY changed)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY changed)
    Q_PROPERTY(int selected READ selected NOTIFY changed)
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(bool hibernationEnabled READ hibernationEnabled NOTIFY changed)
    Q_PROPERTY(int idleMinutes READ idleMinutes NOTIFY changed)
    Q_PROPERTY(QStringList sleepingWorkspaces READ sleepingWorkspaces NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
public:
    explicit SessionConsole(QObject* parent = nullptr);
    ~SessionConsole() override;
    QVariantList tabs() const { return m_tabs; }
    int selected() const { return m_selected; }
    QVariantMap current() const { return m_tabs.value(m_selected).toMap(); }
    enum RuntimeState { Unknown = -1, Ended = 0, Running = 1, Sleeping = 2 };
    Q_ENUM(RuntimeState)
    bool hibernationEnabled() const { return m_sleepEnabled; }
    int idleMinutes() const { return m_minutes; }
    QStringList sleepingWorkspaces() const;
    Q_INVOKABLE void configureHibernation(bool enabled, int minutes);
    Q_INVOKABLE void setDraft(const QString& id, const QString& text);
    Q_INVOKABLE QString draft(const QString& id) const { return m_drafts.value(id); }
    Q_INVOKABLE void setConsoleVisible(bool visible);
    Q_INVOKABLE void scan(const QString& workspace);
    Q_INVOKABLE void resume(int index);
    Q_INVOKABLE void shell(const QString& cwd);
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void poll();
    Q_INVOKABLE void sendNavigationKey(int key);
    Q_INVOKABLE void send(const QString& text);
signals:
    void changed();

private:
    void start(const QString& title, const QString& cwd, const QStringList& command, const QString& origin);
    void save();
    void updateHibernation();
    bool wake(int index);
    bool m_sleepEnabled = false, m_visible = false;
    QHash<QString, QString> m_drafts;
    int m_minutes = 30;
    QHash<QString, qint64> m_since;
    void fail(const QString& message);
    std::unique_ptr<QLockFile> m_writer;
    QVariantList m_history, m_tabs;
    QString m_error;
    int m_selected = -1;
    int m_generation = 0;
    bool m_polling = false;
};
}
