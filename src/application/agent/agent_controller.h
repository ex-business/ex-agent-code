#pragma once
#include "application/services/local_service_process.h"
#include "infrastructure/storage/project_repository.h"
#include "agent/codex/model_catalog.h"
#include <QtQml/qqmlregistration.h>

namespace exagent {
class AgentController final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AgentController)
    QML_UNCREATABLE("由宿主注入")
    Q_PROPERTY(int account READ account NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(bool fullAccess READ fullAccess NOTIFY changed)
    Q_PROPERTY(bool workspaceWrite READ workspaceWrite NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap approval READ approval NOTIFY changed)
    Q_PROPERTY(QStringList models READ models NOTIFY changed)
    Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY changed)
    Q_PROPERTY(bool authenticating READ authenticating NOTIFY changed)
public:
    AgentController(QList<ChatSession> &sessions, ProjectCatalog &catalog, ProjectRepository &repository);
    ~AgentController() override;
    void select(const QString &session, bool writable);
    void restore();
    QStringList models() const { return m_models; }
    QString contextLabel() const;
    Q_INVOKABLE void listModels(int account);
    Q_INVOKABLE QVariantMap previewHistory(int first, int last) const;
    Q_INVOKABLE void includeHistory(int first, int last);
    int account() const;
    QString model() const;
    bool fullAccess() const { return config().value("fullAccess").toBool(); }
    bool workspaceWrite() const;
    bool busy() const;
    QVariantMap approval() const;
    bool authenticating() const { return m_login.state() != QProcess::NotRunning; }
    bool submit(const QString &text, const QString &cwd);
    Q_INVOKABLE void configure(int account, const QString &model, bool write, bool fullAccess = false);
    Q_INVOKABLE void login(QString key);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE void decide(const QString &token, bool allow);
signals:
    void changed();
    void failure(QString reason);
    void accountUpdated(bool loggedIn);
    void rowChanged(QString session, exagent::ChatMessage message);
    void saved();

private:
    ChatSession *find(const QString &id) const;
    QVariantMap config() const;
    bool save();
    void changeCredential(QString key, bool signIn);
    void dispatch();
    bool consume(const QString &method, const QJsonObject &frame, QString *error);
    void end(const QString &session, int state, const QString &detail);
    void put(ChatSession &session, ChatMessage msg);
    struct Active {
        QString id;
        QJsonObject queued;
        QVariantList approvals;
        bool stopping = false;
    };
    QList<ChatSession> &m_sessions;
    ProjectCatalog &m_catalog;
    ProjectRepository &m_repository;
    QString m_selected;
    bool m_writable = false;
    QHash<QString, Active> m_active;
    QHash<QString, QString> m_starts;
    LocalServiceProcess m_worker;
    QHash<QString, QVariantMap> m_contexts;
    QPointer<codex::ModelCatalog> m_catalogQuery;
    QStringList m_models;
    QProcess m_login;
    QByteArray m_key;
};
}
