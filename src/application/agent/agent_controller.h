#pragma once
#include "application/services/local_service_process.h"
#include "infrastructure/storage/project_repository.h"
#include "agent/codex/model_catalog.h"
#include <QtQml/qqmlregistration.h>
#include <QUrl>
#include "plugins/account_profiles/account_profiles.h"

namespace exagent {
class AgentController final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AgentController)
    QML_UNCREATABLE("由宿主注入")
    Q_PROPERTY(int account READ account NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(bool fullAccess READ fullAccess NOTIFY changed)
    Q_PROPERTY(bool workspaceWrite READ workspaceWrite NOTIFY changed)
    Q_PROPERTY(bool stopping READ stopping NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap approval READ approval NOTIFY changed)
    Q_PROPERTY(int pendingApprovalCount READ pendingApprovalCount NOTIFY changed)
    Q_PROPERTY(QStringList models READ models NOTIFY changed)
    Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY changed)
    Q_PROPERTY(bool authenticating READ authenticating NOTIFY changed)
public:
    Q_PROPERTY(QObject *accounts READ accountProfiles CONSTANT)
    QObject *accountProfiles(){return &m_profiles;}
    Q_PROPERTY(QString selectedProfile READ selectedProfile NOTIFY changed)
    QString selectedProfile()const{return accounts::reference(config());}
    Q_PROPERTY(QString runningProfile READ runningProfile NOTIFY changed)
    QString runningProfile()const{return m_active.value(m_selected).snapshot.value("profileId").toString();}
    Q_PROPERTY(bool autoAccountSwitch READ autoAccountSwitch NOTIFY changed)
    bool autoAccountSwitch()const{return config().value("autoAccountSwitch").toInt()==1;}
    Q_INVOKABLE bool useProfile(const QString &id,const QString &model,bool automatic);
    Q_INVOKABLE void loginProfile(const QString &id);
    AgentController(QList<ChatSession> &sessions, ProjectCatalog &catalog, ProjectRepository &repository);
    ~AgentController() override;
    void select(const QString &session, bool writable);
    void restore();
    Q_PROPERTY(QVariantList correspondents READ correspondents NOTIFY mailChanged)
    Q_PROPERTY(QVariantList correspondence READ correspondence NOTIFY mailChanged)
    QVariantList correspondents() const;
    QVariantList correspondence() const;
    Q_INVOKABLE bool sendMail(const QString &target, const QString &text);
    void indexWorkspaceActivity();
    Q_INVOKABLE QVariantMap terminalBinding(const QString& workspace) const;
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
    bool activeSession(const QString &id) const { return m_active.contains(id); }
    QVariantMap executionInfo(const QString &session, const QString &run) const;
    std::function<bool(const ChatSession &)> workspaceAvailable;
    std::function<bool()> accountStorageWritable;
    bool stopping() const {
        const auto run = m_active.constFind(m_selected);
        return run != m_active.cend() && run->stopping;
    }
    QVariantMap approval() const;
    int pendingApprovalCount() const;
    Q_INVOKABLE QVariantList workspaceActivity(bool waitingOnly) const;
    Q_INVOKABLE bool requestPending(const QString &session, const QString &run, const QString &token) const;
    bool authenticating() const { return m_login.state() != QProcess::NotRunning; }
    bool submit(const QString &text, const QString &cwd, const QVariantMap &reviewNote = {});
    QString submitWorker(const QString &session, const QString &text);
    QString submitIndependent(const QString &session, const QString &text);
    enum InputChannel { ChatInput = 1, TerminalInput = 2, MailInput = 3 };
    using InputPolicy = std::function<bool(const ChatSession &, int, QString &, QString *)>;
    bool canTransferInput(const QString &session) const;
    void reconsiderSessionInput(const QString &session);
    void registerInputPolicy(const QString &plugin, QObject *owner, InputPolicy policy, bool global = false);
    bool applyInputPolicies(const ChatSession &session, int channel, QString &text, QString *error) const;
    QVariantMap sessionExtension(const QString &session, const QString &plugin) const;
    bool setSessionExtension(const QString &session, const QString &plugin, const QVariantMap &value, QString *error);
    Q_PROPERTY(QVariantList supervisedTasks READ supervisedTasks NOTIFY supervisionChanged)
    QVariantList supervisedTasks() const;
    Q_PROPERTY(QString workerHost READ workerHost CONSTANT)
    QString workerHost() const;
    Q_INVOKABLE bool exportSupervisedTask(const QString &task, const QString &host, const QUrl &directory);
    Q_INVOKABLE bool importSupervisedExchange(const QUrl &file, const QString &worker);

    Q_INVOKABLE bool createSupervisedTask(const QString &target, const QString &text, bool decisionRequired);
    Q_INVOKABLE bool dispatchSupervisedTask(const QString &task);
    Q_INVOKABLE bool resolveSupervisedTask(const QString &task, bool allow);


    Q_INVOKABLE void configure(int account, const QString &model, bool write, bool fullAccess = false);
    Q_PROPERTY(QString credentialFile READ credentialFile NOTIFY changed)
    QString credentialFile() const { return config().value("credentialFile").toString(); }
    Q_INVOKABLE bool configureApi(const QString &model, const QString &file, bool write);
    Q_INVOKABLE void login(QString key);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE void decide(const QString &token, bool allow);
signals:
    void approvalArrived(QString session, QString run, QString token, QString title);
    void runEnded(QString session, QString run, int state, QString title);
    void changed();
    void mailChanged();
    void supervisionChanged();
    void failure(QString reason);
    void accountUpdated(bool loggedIn);
    void rowChanged(QString session, exagent::ChatMessage message);
    void saved();

private:
    bool submitTo(const QString &session, const QString &text, const QString &cwd,
        const QVariantMap &reviewNote, bool worker);
    ChatSession *find(const QString &id) const;
    QVariantMap config() const;
    bool save();
    void changeCredential(QString key, bool signIn);
    void dispatch();
    bool enqueueMail(const QString &source, const QString &target, const QString &text, const QString &id);
    void deliverMail(const QString &session);
    void settleMail(const QString &session, const QString &run, const QString &id, int state);
    void recoverMail(ChatSession &session);
    void finishSupervised(const QString &worker, const QString &run, int state);
    void deliverSupervisedResults();
    void updateProfileBinding(ChatSession &session,const QString &id,const QVariantMap &binding);
    QString m_loginProfile;
    QTimer m_supervisionPump;
    QHash<QString, QString> m_mailRequests;
    bool consume(const QString &method, const QJsonObject &frame, QString *error);
    void end(const QString &session, int state, const QString &detail);
    void put(ChatSession &session, ChatMessage msg);
    struct Active {
        QString id;
        QJsonObject queued;
        QVariantList approvals;
        bool stopping = false;
        QString turn, mail;
        bool reconciliation = false;
        QJsonObject snapshot;
        QJsonObject usage = {};
    };
    struct Policy { QPointer<QObject> owner; InputPolicy apply; bool global = false; };
    QHash<QString, Policy> m_inputPolicies;
    QList<ChatSession> &m_sessions;
    ProjectCatalog &m_catalog;
    ProjectRepository &m_repository;
    accounts::Profiles m_profiles;
    QString m_selected;
    QString m_workerHost;
    bool m_writable = false;
    QHash<QString, Active> m_active;
    QHash<QString, qsizetype> m_latestMarker;
    QHash<QString, QString> m_starts;
    LocalServiceProcess m_worker;
    QHash<QString, QVariantMap> m_contexts;
    QPointer<codex::ModelCatalog> m_catalogQuery;
    QStringList m_models;
    QProcess m_login;
    QByteArray m_key;
};
}
