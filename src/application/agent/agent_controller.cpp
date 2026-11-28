#include "agent_controller.h"
#include "plugins/design_mode/design_selection.h"
#include "history_context.h"
#include "agent/openai/credentials.h"
#include "agent/runtime_run.h"
#include "plugins/session_mail/mail.h"
#include "plugins/supervision/task_state.h"
#include "agent/codex/codex_account.h"
#include "agent/codex/thread_input.h"
#include <QDir>
#include <QJsonArray>
#include <QFileInfo>
#include <QUuid>
#include <QSettings>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
namespace exagent {
using State = content::TaskState;
namespace {
    QString newId()
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    MessageStatus messageStatus(int state)
    {
        switch (State(state)) {
        case State::Complete:
            return MessageStatus::Complete;
        case State::Cancelled:
            return MessageStatus::Cancelled;
        case State::Created:
        case State::Running:
            return MessageStatus::Running;
        default:
            return MessageStatus::Failed;
        }
    }
}
AgentController::AgentController(
    QList<ChatSession> &sessions, ProjectCatalog &catalog, ProjectRepository &repository)
    : m_sessions(sessions)
    , m_catalog(catalog)
    , m_repository(repository)
    , m_profiles(this)
    , m_worker(ServiceLaunch { {}, { "--agent-worker" }, "codex-agent",
                   { "system.ping", "stream.resume", "agent.start", "agent.stop", "agent.approve", "agent.steer" } },
          this)
{
    m_profiles.mayWrite=[this]{return m_repository.writable() && (!accountStorageWritable || accountStorageWritable());};
    connect(&m_profiles,&accounts::Profiles::failure,this,&AgentController::failure);
    QSettings settings;
    m_workerHost = settings.value("supervision/hostId").toString();
    if (QUuid(m_workerHost).isNull()) {
        m_workerHost = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("supervision/hostId", m_workerHost);
    }
    m_supervisionPump.setSingleShot(true);
    connect(&m_supervisionPump, &QTimer::timeout, this, &AgentController::deliverSupervisedResults);
    m_worker.setEventSink(
        [this](const auto &method, const auto &frame, QString *error) { return consume(method, frame, error); });
    connect(&m_worker, &LocalServiceProcess::ready, this, &AgentController::dispatch);
    connect(&m_worker, &LocalServiceProcess::failure, this, [this](const QString &reason) {
        const auto sessions = m_active.keys();
        for (const auto &id : sessions)
            end(id, int(State::Interrupted), reason);
    });
    connect(&m_worker, &LocalServiceProcess::response, this,
        [this](const QString &id, const QJsonValue &) { m_starts.remove(id); m_mailRequests.remove(id); });
    connect(&m_worker, &LocalServiceProcess::requestFailed, this,
        [this](const QString &id, int code, const QString &reason) {
            const auto messageId = m_mailRequests.take(id);
            if (!messageId.isEmpty()) {
                for (const auto &session : m_active.keys()) {
                    const auto active = m_active.value(session);
                    if (active.mail == messageId) {
                        settleMail(session, active.id, messageId, code == -32602 ? mail::Rejected : mail::Unconfirmed);
                        break;
                    }
                }
                return;
            }
            const auto runId = m_starts.take(id);
            if (!runId.isEmpty()) {
                for (const auto &session : m_active.keys())
                    if (m_active.value(session).id == runId) {
                        end(session, int(State::Interrupted), reason);
                        break;
                    }
            } else
                emit failure(reason);
        });
    connect(&m_login, &QProcess::started, this, [this] {
        if (!m_key.isEmpty()) m_login.write(m_key);
        m_login.closeWriteChannel();
        m_key.fill(0);
        m_key.clear();
    });
    connect(&m_login, &QProcess::stateChanged, this, &AgentController::changed);
    connect(&m_login, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_key.fill(0);
            m_key.clear();
            m_loginProfile.clear();
            emit failure(QStringLiteral("无法启动专用账号操作。"));
        }
    });
    connect(&m_login, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!code && status == QProcess::NormalExit) {
            if(m_loginProfile.isEmpty()){codex::noteDedicatedLogin();m_profiles.resetAvailability("dedicated");}
            else m_profiles.resetAvailability(m_loginProfile);
            m_loginProfile.clear();
            emit accountUpdated(m_login.arguments().contains("login"));
        } else
            emit failure(QStringLiteral("专用账号操作失败，请检查 Codex CLI 和系统凭据存储。"));
        m_loginProfile.clear();
    });
}
AgentController::~AgentController()
{
    m_worker.disconnect(this);
    m_worker.setEventSink({});
    m_worker.stop();
    m_login.disconnect(this);
    m_key.fill(0);
}
void AgentController::select(const QString &session, bool writable)
{
    const bool mailboxChanged = m_selected != session || m_writable != writable;
    m_selected = session;
    m_writable = writable;
    if (mailboxChanged) { emit mailChanged(); emit supervisionChanged(); }
    emit changed();
}
ChatSession *AgentController::find(const QString &id) const
{
    for (auto &s : m_sessions)
        if (s.id == id)
            return &s;
    return nullptr;
}
QVariantMap AgentController::config() const
{
    const auto *s = find(m_selected);
    return s ? s->agent : QVariantMap {};
}
int AgentController::account() const
{
    return config().value("account").toInt();
}
QString AgentController::model() const
{
    return config().value("model").toString();
}
bool AgentController::workspaceWrite() const
{
    return config().value("write").toBool();
}
bool AgentController::busy() const
{
    return m_active.contains(m_selected);
}
QVariantMap AgentController::approval() const
{
    const auto it = m_active.constFind(m_selected);
    return it != m_active.cend() && !it->approvals.isEmpty() ? it->approvals.first().toMap() : QVariantMap {};
}
bool AgentController::save()
{
    QString error;
    if (!m_repository.save(m_catalog, m_sessions, &error)) {
        emit failure(error);
        return false;
    }
    return true;
}
bool AgentController::configureApi(const QString &model, const QString &file, bool write)
{
    auto *s=find(m_selected);
    if(s && !s->history.value("terminalSource").toMap().isEmpty()) {emit failure(QStringLiteral("此会话使用终端账号，不能设置原生API执行器。"));return false;}
    if(!s || !m_writable || busy() || authenticating() || model.trimmed().isEmpty() || model.size()>128 || !codex::threadAvailable(s->agent.value("threadId").toString())) return false;
    QString error;auto key=openai::credential(file,&error);const bool valid=!key.isEmpty();key.fill(0);
    if(!valid){emit failure(error);return false;}
    const auto managed=m_profiles.profile(s->agent.value("accountProfile").toString());
    const bool preserveProfile=managed["kind"]==3 && managed["credentialFile"]==file;
    if(!preserveProfile&&!m_profiles.legacyApi(file,model.trimmed()))return false;
    const auto before=s->agent;
    updateProfileBinding(*s,accounts::reference(s->agent),s->agent);
    if(!preserveProfile)s->agent.remove("accountProfile");
    // 自有账号类型3为Responses适配器；切换不覆盖Codex的线程标识。
    s->agent.insert("account",agent::OpenAIResponses);s->agent.insert("model",model.trimmed());s->agent.insert("credentialFile",file);
    s->agent.insert("write",write);s->agent.insert("fullAccess",false);
    for(const auto &key:{"threadId","identity","boundAccount","boundCwd","responseId","apiCwd"})s->agent.remove(key);
    const auto binding=s->agent.value("profileBindings").toMap().value(accounts::reference(s->agent)).toMap();
    for(auto it=binding.begin();it!=binding.end();++it)s->agent.insert(it.key(),it.value());
    if(!save()){s->agent=before;return false;}
    emit changed();return true;
}
void AgentController::configure(int account, const QString &model, bool write, bool fullAccess)
{
    auto *s = find(m_selected);
    if (!s || !m_writable || busy() || account < 0 || account > 2 || model.size() > 128)
        return;
    if (!s->history.value("terminalSource").toMap().isEmpty()) {
        emit failure(QStringLiteral("此会话使用终端适配器，请通过续接当前聊天打开对应 Agent；尚未接入其原生执行器。"));return;
    }
    if (!codex::threadAvailable(s->agent.value("threadId").toString())) {
        emit failure(QStringLiteral("请先退出正在使用此线程的终端，再修改会话配置。"));
        return;
    }
    updateProfileBinding(*s,accounts::reference(s->agent),s->agent);
    if(m_profiles.profile(s->agent.value("accountProfile").toString())["kind"].toInt()!=account)s->agent.remove("accountProfile");
    s->agent.insert("account", account);
    s->agent.insert("model", model.trimmed());
    s->agent.insert("write", write || fullAccess);
    s->agent.insert("fullAccess", fullAccess);
    for(const auto &key:{"threadId","identity","boundAccount","boundCwd","responseId","apiCwd"})s->agent.remove(key);
    const auto binding=s->agent.value("profileBindings").toMap().value(accounts::reference(s->agent)).toMap();
    for(auto it=binding.begin();it!=binding.end();++it)s->agent.insert(it.key(),it.value());
    save();
    emit changed();
}
QString AgentController::contextLabel() const
{
    const auto ctx = m_contexts.value(m_selected);
    return ctx.isEmpty() ? QString {}
                         : QStringLiteral("附带历史 · 第 %1 至 %2 回合")
                               .arg(ctx.value("first").toInt() + 1)
                               .arg(ctx.value("last").toInt() + 1);
}
QVariantMap AgentController::previewHistory(int first, int last) const
{
    const auto *s = find(m_selected);
    return s ? historyContext(*s, m_repository.sessionDirectory(s->projectId, s->id), first, last)
             : QVariantMap {};
}
void AgentController::includeHistory(int first, int last)
{
    if (busy())
        return;
    if (first < 0)
        m_contexts.remove(m_selected);
    else {
        const auto ctx = previewHistory(first, last);
        if (ctx.contains("error")) {
            emit failure(ctx.value("error").toString());
            return;
        }
        m_contexts.insert(m_selected, ctx);
    }
    emit changed();
}
void AgentController::listModels(int account)
{
    if (m_catalogQuery) {
        m_catalogQuery->disconnect(this);
        m_catalogQuery->deleteLater();
        m_catalogQuery = nullptr;
    }
    m_models.clear();
    emit changed();
    if (account != codex::System && account != codex::Dedicated)
        return;
    auto *query = new codex::ModelCatalog(codex::Account(account), this);
    m_catalogQuery = query;
    connect(query, &codex::ModelCatalog::loaded, this, [this, query](const QStringList &list) {
        m_models = list;
        query->disconnect(this);
        query->deleteLater();
        emit changed();
    });
    connect(query, &codex::ModelCatalog::failed, this, [this, query](const QString &reason) {
        query->disconnect(this);
        query->deleteLater();
        emit failure(reason);
    });
    query->start();
}
void AgentController::login(QString key)
{
    changeCredential(std::move(key),true);
}
void AgentController::logout()
{
    changeCredential({},false);
}
void AgentController::changeCredential(QString key,bool signIn)
{
    if (!m_writable || authenticating() || !m_active.isEmpty()) {
        emit failure(QStringLiteral("请先结束活动运行，再更改专用账号。"));
        return;
    }
    if (signIn && (key.trimmed().isEmpty() || key.size() > 4096)) {
        emit failure(QStringLiteral("请输入有效的专用 Key。")); return;
    }
    QString error;
    if (!codex::prepare(m_login, codex::Dedicated, &error)) {
        emit failure(error);
        return;
    }
    m_loginProfile.clear();
    if (signIn) m_key = key.trimmed().toUtf8() + '\n';
    key.fill(QChar());
    m_login.setWorkingDirectory(QDir::homePath());
    m_login.setStandardOutputFile(QProcess::nullDevice());
    m_login.setArguments(codex::accountArguments(codex::Dedicated) + (signIn ? QStringList { "login", "--with-api-key" } : QStringList{"logout"}));
    m_login.start();
}
void AgentController::put(ChatSession &s, ChatMessage msg)
{
    auto it = std::find_if(s.messages.begin(), s.messages.end(), [&](const auto &m) { return m.id == msg.id; });
    if (it == s.messages.end())
        s.messages.append(msg);
    else
        *it = msg;
    s.updatedAt = QDateTime::currentDateTimeUtc();
    emit rowChanged(s.id, msg);
}
QVariantMap AgentController::terminalBinding(const QString& workspace) const
{
    auto binding = config();
    auto* s = find(m_selected);
    if (s && m_writable && !m_syncing.contains(s->id) && !s->history.value("terminalSource").toMap().isEmpty()) {
        if (workspaceAvailable && !workspaceAvailable(*s))return {};
        const auto *owner=m_catalog.workspace(s->projectId,s->workspaceId);
        if(owner && owner->launch.value("host","local").toString()!="local")return {};
        auto source=s->history.value("terminalSource").toMap();
        if(!workspace.isEmpty() && QFileInfo(workspace).canonicalFilePath()!=QFileInfo(source.value("cwd").toString()).canonicalFilePath())return {};
        QString input,error;if(!applyInputPolicies(*s,TerminalInput,input,&error))return {};
        auto spec=source;
        spec.insert("source",source);spec.insert("conversationId",s->id);spec.insert("native",s->history.value("nativeAgent"));return spec;
    }
    if (!s || !m_writable || busy() || m_syncing.contains(s->id) || authenticating() || !m_profiles.home(selectedProfile()).isEmpty() || account() == agent::OpenAIResponses || !account()
        || binding.value("boundAccount").toInt() != account() || binding.value("identity").toString().isEmpty()
        || QUuid(binding.value("threadId").toString()).isNull()) return {};
    if (workspaceAvailable && !workspaceAvailable(*s)) return {};
    const auto *bindingWorkspace = m_catalog.workspace(s->projectId, s->workspaceId);
    if (bindingWorkspace && bindingWorkspace->launch.value("host", "local").toString() != "local") return {};
    const auto cwd = workspace.isEmpty() ? m_repository.sessionDirectory(s->projectId,s->id) + "/chat-cwd" : workspace;
    if (QFileInfo(cwd).canonicalFilePath() != binding.value("boundCwd").toString()) return {};
    QString input, policyError;
    if (!applyInputPolicies(*s, TerminalInput, input, &policyError)) return {};
    QVariantMap spec;
    for (const auto& key : {"threadId","boundCwd","boundAccount","identity","model","write","fullAccess"})
        spec.insert(key,binding.value(key));
    spec.insert("profileId",selectedProfile());
    spec.insert("conversationId",s->id);
    spec.insert("program", codex::executable());
    spec.insert("home", qEnvironmentVariable("CODEX_HOME", QDir::home().filePath(".codex")));
    return spec;
}
bool AgentController::canTransferInput(const QString &session) const
{
    const auto *s=find(session);
    // 已在原生聊天运行的任务可共享；外部终端占有线程时不能声称已交接独占输入。
    return s && (activeSession(session) || codex::threadAvailable(s->agent.value("threadId").toString()));
}
void AgentController::reconsiderSessionInput(const QString &session)
{
    deliverMail(session);
}
void AgentController::registerInputPolicy(const QString &plugin, QObject *owner, InputPolicy policy, bool global)
{
    m_inputPolicies.insert(plugin, {owner, std::move(policy), global});
}
bool AgentController::applyInputPolicies(const ChatSession &session, int channel, QString &text, QString *error) const
{
    // 运行期守卫先检查租约，再沿用原有会话绑定策略；默认注册仍只作用于绑定会话。
    for (const auto &guard : m_inputPolicies)
        if (guard.global && guard.owner && !guard.apply(session, channel, text, error)) return false;
    const auto extensions = session.history.value("inputExtensions").toMap();
    if (extensions.isEmpty()) return true;
    // 扩展只作用于显式绑定的会话；插件缺失或注册表损坏时不降级绕过其输入约束。
    for (auto it = extensions.begin(); it != extensions.end(); ++it) {
        const auto policy = m_inputPolicies.constFind(it.key());
        if (policy == m_inputPolicies.cend() || !policy->owner) {
            *error = QStringLiteral("此会话依赖的输入插件尚未就绪：") + it.key();
            return false;
        }
        if (!policy->apply(session, channel, text, error)) return false;
    }
    if (text.size() > 65536) { *error = QStringLiteral("扩展上下文与输入合计超过 64K 字符。"); return false; }
    return true;
}
QVariantMap AgentController::sessionExtension(const QString &id, const QString &plugin) const
{
    const auto *s = find(id);
    return s ? s->history.value("inputExtensions").toMap().value(plugin).toMap() : QVariantMap{};
}
bool AgentController::setSessionExtension(const QString &id, const QString &plugin, const QVariantMap &value, QString *error)
{
    auto *s = find(id);
    static const QRegularExpression name("^[a-z][a-z0-9-]{0,63}$");
    if (!s || !m_repository.writable() || activeSession(id) || !name.match(plugin).hasMatch() ||
        QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact).size() > 8192) {
        *error = QStringLiteral("会话扩展绑定无效或当前不能修改。"); return false;
    }
    const auto before = s->history;
    auto extensions = before.value("inputExtensions").toMap();
    if (extensions.contains(plugin) && extensions.value(plugin).toMap() != value) {
        *error = QStringLiteral("会话已绑定此插件的另一个对象。"); return false;
    }
    extensions.insert(plugin, value); s->history.insert("inputExtensions", extensions);
    if (!save()) { s = find(id); if (s) s->history = before; *error = QStringLiteral("会话扩展保存失败。"); return false; }
    return true;
}
bool AgentController::submit(const QString &text, const QString &cwd, const QVariantMap &reviewNote)
{
    return submitTo(m_selected, text, cwd, reviewNote, false);
}
QString AgentController::submitWorker(const QString &session, const QString &text)
{
    // 监督派发沿用目标会话的权限和目录，独立输入不消费目标草稿、历史选择或附件。
    const auto *target = find(session);
    const auto *source = find(m_selected);
    if (!source || !target || source->projectId != target->projectId || session == m_selected) return {};
    return submitIndependent(session, text);
}
QString AgentController::submitIndependent(const QString &session, const QString &text)
{
    const auto *target = find(session);
    if (!target || text.size() > 65536) return {};
    const auto *workspace = m_catalog.workspace(target->projectId, target->workspaceId);
    if (!submitTo(session, text, workspace ? workspace->path : QString{}, {}, true)) return {};
    return m_active.value(session).id;
}
bool AgentController::submitTo(const QString &session, const QString &text, const QString &cwd,
    const QVariantMap &reviewNote, bool worker)
{
    auto *s = find(session);
    if (s && workspaceAvailable && !workspaceAvailable(*s)) {
        emit failure(QStringLiteral("工作区已归档、已清理或正在处理中。"));
        return false;
    }
    const auto *workspace = s ? m_catalog.workspace(s->projectId, s->workspaceId) : nullptr;
    if (s && !s->workspaceId.isEmpty() && !workspace) return false;
    if (workspace && workspace->launch.value("host", "local").toString() != "local") {
        emit failure(QStringLiteral("当前执行器无法在此任务绑定的主机运行。"));
        return false;
    }
    if(s && !s->history.value("terminalSource").toMap().isEmpty() && !text.trimmed().isEmpty() && text.size()<=65536)
        return submitNative(session,text,reviewNote,worker);
    if (!s || !(worker ? m_repository.writable() : m_writable) || !s->agent.value("account").toInt() || text.trimmed().isEmpty())
        return false;
    if (!s->history.value("terminalSource").toMap().isEmpty()) {
        emit failure(QStringLiteral("此会话使用终端适配器，请通过续接当前聊天打开对应 Agent；尚未接入其原生执行器。"));return false;
    }
    if (!codex::threadAvailable(s->agent.value("threadId").toString())) {
        emit failure(QStringLiteral("会话正在另一入口中使用。请先在交互终端退出 Codex，再继续聊天。"));
        return false;
    }
    if (m_active.contains(session) || m_syncing.contains(session) || authenticating()) {
        emit failure(QStringLiteral("请等待当前运行或账号登录结束。"));
        return false;
    }
    QString effectivePrompt = text, policyError;
    if (!applyInputPolicies(*s, ChatInput, effectivePrompt, &policyError)) {
        emit failure(policyError); return false;
    }
    if(!m_profiles.usageReady()){emit failure(QStringLiteral("账号用量仍在载入或存在损坏，请核对账号面板后重试。"));return false;}
    QVariantMap runConfig=s->agent;
    const auto requested=accounts::reference(runConfig);
    if(runConfig.value("account").toInt()==3 && m_profiles.profile(requested).isEmpty())m_profiles.legacyApi(runConfig.value("credentialFile").toString(),runConfig.value("model").toString());
    const auto selected=m_profiles.route(requested,runConfig.value("model").toString(),runConfig.value("autoAccountSwitch").toInt()==1);
    if(selected.isEmpty()){emit failure(QStringLiteral("所选账号不可用，且没有已验证的限额候选账号。当前消息未派发。"));return false;}
    const auto profile=selected["id"].toString();
    runConfig.insert("account",selected["kind"].toInt());runConfig.insert("credentialFile",selected["credentialFile"].toString());
    if(profile!=requested) {
        for(const auto &key:{"threadId","identity","boundAccount","boundCwd","responseId","apiCwd"})runConfig.remove(key);
        const auto binding=s->agent.value("profileBindings").toMap().value(profile).toMap();for(auto it=binding.begin();it!=binding.end();++it)runConfig.insert(it.key(),it.value());
    }
    if(!codex::threadAvailable(runConfig.value("threadId").toString())){emit failure(QStringLiteral("候选账号的线程正由终端占用。"));return false;}
    QString directory;
    if (cwd.isEmpty()) {
        if (runConfig.value("write").toBool()) {
            emit failure(QStringLiteral("写入文件需要绑定工作区。只想聊天时，可在会话执行设置中关闭工作区写入。"));
            return false;
        }
        // 独立的空目录为普通聊天提供稳定的 cwd，避免默认从用户主目录启动。
        const auto scratch = m_repository.sessionDirectory(s->projectId, s->id) + "/chat-cwd";
        if (QFileInfo(scratch).isSymLink() || !QDir().mkpath(scratch)
            || !QFile::setPermissions(scratch, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            emit failure(QStringLiteral("无法建立聊天运行目录，草稿已保留。"));
            return false;
        }
        directory = QFileInfo(scratch).canonicalFilePath();
    } else {
        const QFileInfo folder(cwd);
        if (folder.isAbsolute() && folder.isDir())
            directory = folder.canonicalFilePath();
    }
    if (directory.isEmpty()) {
        emit failure(QStringLiteral("当前工作区目录不可用，请检查目录或切换工作区。"));
        return false;
    }
    if (m_worker.state() == ipc::ServiceState::Failed && m_worker.processId() > 0) {
        emit failure(QStringLiteral("Agent worker 正在结束，请稍后再试。"));
        return false;
    }
    const auto run = newId();
    QJsonObject spec { { "conversationId", s->id }, { "runId", run }, { "prompt", effectivePrompt }, { "cwd", directory },
        { "model", runConfig.value("model").toString() }, { "write", runConfig.value("write").toBool() }, { "fullAccess", runConfig.value("fullAccess").toBool() }, { "account", runConfig.value("account").toInt() } };
    if (runConfig.value("boundAccount").toInt() == runConfig.value("account").toInt() && runConfig.value("boundCwd").toString() == directory)
        spec.insert("threadId", runConfig.value("threadId").toString());
    spec.insert("identity", runConfig.value("identity").toString());
    spec.insert("profileId",profile);spec.insert("accountHome",m_profiles.home(profile));
    if(runConfig.value("account").toInt()==agent::OpenAIResponses) {
        spec.remove("threadId");spec.insert("credentialFile",runConfig.value("credentialFile").toString());
        if(runConfig.value("apiCwd").toString()==directory)spec.insert("responseId",runConfig.value("responseId").toString());
    }
    // 批注是独立输入；普通草稿选中的历史和图片只随该草稿派发。
    const auto ctx = !worker && reviewNote.isEmpty() ? m_contexts.value(s->id) : QVariantMap{};
    if (!ctx.isEmpty()) {
        const auto current = previewHistory(ctx.value("first").toInt(), ctx.value("last").toInt());
        if (current != ctx) {
            emit failure(QStringLiteral("所选历史已变化，请重新预览。"));
            return false;
        }
        spec.insert("context", ctx.value("text").toString());
        spec.insert("images", QJsonArray::fromStringList(ctx.value("images").toStringList()));
    }
    ChatMessage user;
    user.id = newId();
    user.content = text;
    // 页面附件只随普通草稿发送，与批注和手动选择的历史保持各自归属。
    if (!worker && reviewNote.isEmpty()) {
        QString path;
        const auto error = design::apply(design::pending(*s), m_repository.sessionDirectory(s->projectId,s->id), user, path);
        if (!error.isEmpty()) { emit failure(error); return false; }
        if (!path.isEmpty()) {
            auto images = spec.value("images").toArray();
            images.append(path);
            spec.insert("images",images);
            spec.insert("designElement", QJsonObject::fromVariantMap(user.activity.value("designElement").toMap()));
        }
    }
    if(runConfig.value("account").toInt()==agent::OpenAIResponses && !spec.value("images").toArray().isEmpty()) {
        emit failure(QStringLiteral("此 API 适配器暂不发送图片，请移除附件后重试。")); return false;
    }
    ChatMessage marker;
    marker.id = run;
    marker.role = MessageRole::Notice;
    marker.status = MessageStatus::Running;
    marker.content = runConfig.value("account").toInt()==agent::OpenAIResponses ? QStringLiteral("OpenAI API 正在准备执行") : QStringLiteral("Codex 正在准备执行");
    marker.activity = { { "serviceId", "codex-agent" }, { "taskState", int(State::Created) }, { "cwd", directory },
        { "account", runConfig.value("account").toInt() }, { "requestedModel", runConfig.value("model").toString() } };
    marker.activity.insert("profileId",profile);
    // 用户输入与派发意图必须一起写入。失败时保留草稿，不能让 worker 收到半次派发。
    const auto before = *s;
    if (s->messages.isEmpty())
        s->title = text.simplified().left(32);
    if (!reviewNote.isEmpty()) user.activity = {{"reviewNote", reviewNote}, {"reviewRunId", run}};
    s->messages.append(user);
    s->messages.append(marker);
    if (!worker && reviewNote.isEmpty()) {
        design::setPending(*s, {});
        s->draft.clear();
    } else if (!reviewNote.isEmpty()) {
        // 与发送消息一起落盘，失败时整次回滚，同时保留输入框原有草稿。
        auto notes = s->history.value("reviewNotes").toList();
        if (!notes.removeOne(reviewNote)) { *s = before; return false; }
        s->history.insert("reviewNotes", notes);
    }
    s->updatedAt = marker.createdAt;
    if (!save()) {
        *s = before;
        return false;
    }
    if (!worker && reviewNote.isEmpty()) m_contexts.remove(s->id);
    m_latestMarker.insert(s->id, s->messages.size() - 1);
    m_active.insert(s->id, { run, spec, {}, false, {}, {}, false, spec });
    emit rowChanged(s->id, user);
    emit rowChanged(s->id, marker);
    emit saved();
    emit changed();
    if (m_worker.state() == ipc::ServiceState::Ready)
        dispatch();
    else
        m_worker.start();
    return true;
}
void AgentController::dispatch()
{
    for (const auto &session : m_active.keys()) deliverMail(session);
    for (const auto &session : m_active.keys()) {
        auto &a = m_active[session];
        if (a.queued.isEmpty())
            continue;
        const auto request = m_worker.request("agent.start", a.queued);
        if (request.isEmpty()) {
            end(session, int(State::Failed), QStringLiteral("派发队列不可用。"));
            continue;
        }
        m_starts.insert(request, a.id);
        a.queued = {};
    }
}
void AgentController::reconcile()
{
    auto *s = find(m_selected);
    if (!s || !m_writable || busy())
        return;
    if(!s->history.value("terminalSource").toMap().isEmpty()){auto binding=s->history.value("terminalSource").toMap();binding.insert("source",binding);binding.insert("conversationId",s->id);syncTerminalConversation(binding);return;}
    if(s->agent.value("account").toInt()==agent::OpenAIResponses) {emit failure(QStringLiteral("API 中断后可发送新消息续接最近完成的响应；不会重放待审批工具。"));return;}
    for (auto it = s->messages.crbegin(); it != s->messages.crend(); ++it) {
        const auto &m = *it;
        if (m.role != MessageRole::Notice || m.activity.value("serviceId") != "codex-agent")
            continue;
        const auto thread = m.activity.value("threadId").toString(), turn = m.activity.value("turnId").toString();
        if (thread.isEmpty() || turn.isEmpty())
            break;
        QJsonObject spec { { "reconcile", true }, { "conversationId", s->id }, { "runId", m.id },
            { "cwd", m.activity.value("cwd").toString() }, { "identity", m.activity.value("identity").toString() },
            { "account", m.activity.value("account").toInt() }, { "threadId", thread }, { "turnId", turn } };
        spec.insert("profileId",m.activity.value("profileId",accounts::reference({{"account",m.activity.value("account")}})).toString());
        spec.insert("accountHome",m_profiles.home(spec["profileId"].toString()));
        m_active.insert(s->id, { m.id, spec, {}, false, {}, {}, true, spec });
        emit changed();
        if (m_worker.state() == ipc::ServiceState::Ready)
            dispatch();
        else
            m_worker.start();
        return;
    }
    emit failure(QStringLiteral("此会话没有同时具备线程和回合标识的执行记录。"));
}
void AgentController::stop()
{
    auto it = m_active.find(m_selected);
    if (it == m_active.end() || it->stopping)
        return;
    if (!it->queued.isEmpty()) {
        end(m_selected, int(State::Cancelled), QStringLiteral("派发已取消"));
        return;
    }
    if (m_worker.request("agent.stop", { { "runId", it->id } }).isEmpty()) {
        emit failure(QStringLiteral("停止请求未送达。"));
        return;
    }
    // 停止请求已经排队，未处理的授权不能再放行工具。
    it->stopping = true;
    it->approvals.clear();
    emit changed();
}
void AgentController::decide(const QString &token, bool allow)
{
    auto it = m_active.find(m_selected);
    if (it == m_active.end() || it->stopping || approval().value("token").toString() != token || token.isEmpty())
        return;
    if (m_worker.request("agent.approve", { { "runId", it->id }, { "token", token }, { "allow", allow } })
            .isEmpty()) {
        emit failure(QStringLiteral("审批请求未送达。"));
        return;
    }
    it->approvals.removeFirst();
    emit changed();
}
void AgentController::end(const QString &session, int state, const QString &detail)
{
    const auto a = m_active.take(session);
    if (!a.mail.isEmpty()) settleMail(session, a.id, a.mail, mail::Unconfirmed);
    auto *s = find(session);
    if (!s || a.id.isEmpty())
        return;
    // 先更新全部活动行再广播，当前视图同步不能让其他会话的输出覆盖本会话。
    QList<ChatMessage> updates;
    for (auto &m : s->messages) {
        if (m.id != a.id && (m.activity.value("runId").toString() != a.id || m.status != MessageStatus::Running))
            continue;
        m.status = messageStatus(state);
        m.activity.insert("taskState", state);
        if (m.id == a.id) {
            if(!a.usage.isEmpty())m.activity.insert("usage",a.usage.toVariantMap());
            m.content = detail;
            m.activity.insert("finishedAt", QDateTime::currentDateTimeUtc());
        }
        updates.append(m);
    }
    for (const auto &m : updates)
        emit rowChanged(session, m);
    save();
    emit changed();
    finishSupervised(session, a.id, state);
    emit runEnded(session, a.id, state, s->title);
    if(!a.snapshot["source"].toObject().isEmpty()) {
        auto binding=a.snapshot["source"].toObject().toVariantMap();binding.insert("source",binding);binding.insert("conversationId",session);
        QTimer::singleShot(0,this,[this,binding]{syncTerminalConversation(binding);});
    }
}
void AgentController::restore()
{
    QSet<QString> migrated;
    // 迁移只登记尚未存在的凭据引用；不在每次打开时反复保存，也不改写用户启停/限额选择。
    for(const auto &session:m_sessions) {
        if(session.agent.value("account").toInt()!=3 || !session.agent.value("accountProfile").toString().isEmpty())continue;
        const auto id=accounts::reference(session.agent);
        if(!m_profiles.profile(id).isEmpty()||migrated.contains(id))continue;
        migrated.insert(id);
        if(!m_profiles.legacyApi(session.agent.value("credentialFile").toString(),session.agent.value("model").toString()))break;
    }

    // 宿主重启无法持有旧连接。保留线程与回合供后续恢复，不重放已发送的提示词。
    for (auto &s : m_sessions) {
        recoverMail(s);
        auto tasks = s.history.value("supervisedTasks").toList();
        for (auto &value : tasks) {
            auto row = value.toMap();
            // 重启后没有原连接，保留派发标识与输出，不自动重放执行。
            if (row.value("state").toInt() == supervision::Working) {
                row.insert("state", supervision::Uncertain);
                auto attempts = row.value("attempts").toList();
                if (!attempts.isEmpty()) {
                    auto attempt = attempts.last().toMap();
                    attempt.insert("state", supervision::Uncertain);
                    attempts.last() = attempt;
                    row.insert("attempts", attempts);
                }
                value = row;
            }
        }
        if (!tasks.isEmpty()) s.history.insert("supervisedTasks", tasks);
    }
    m_supervisionPump.start(1000);
    for (auto &s : m_sessions)
        for (auto &m : s.messages)
            if (m.activity.value("serviceId") == "codex-agent" && m.status == MessageStatus::Running) {
                m.status = MessageStatus::Failed;
                m.activity.insert("taskState", int(State::Interrupted));
                if (m.role == MessageRole::Notice)
                    m.content = QStringLiteral("上次执行结果待核对");
            }
}
bool AgentController::consume(const QString &, const QJsonObject &e, QString *error)
{
    const auto session = e["conversationId"].toString();
    auto *s = find(session);
    const auto it = m_active.find(session);
    if (!s || it == m_active.end() || it->id != e["runId"].toString())
        return true;
    const auto kind = e["kind"].toString();
    const auto profile=it->snapshot["profileId"].toString();
    if(kind=="accountInfo"||kind=="accountLimits"||kind=="accountUnavailable"){m_profiles.observe(profile,e);return true;}
    if(kind=="usage"){
        m_profiles.usage(profile,it->id,e["usage"].toObject());
        // 保存在运行快照中，结束时随现有行更新落盘，避免视图缓存覆盖或每次计数重排聊天。
        it->usage=e["usage"].toObject();
        return true;
    }
    if(kind=="nativeCheckpoint") {
        const auto source=it->snapshot["source"].toObject();
        if(source.isEmpty() || source["id"]!=e["sourceId"] || !e["entries"].isArray() || e["entries"].toArray().size()>10000){*error=QStringLiteral("原生CLI检查点无效。");return false;}
        auto marker=std::find_if(s->messages.begin(),s->messages.end(),[&](const auto &m){return m.id==it->id;});
        if(marker!=s->messages.end()){auto row=*marker;row.activity.insert("nativeEntries",e["entries"].toArray().toVariantList());put(*s,row);save();}return true;
    }
    if(kind=="checkpoint") {
        if(it->snapshot["account"].toInt()!=agent::OpenAIResponses || e["responseId"].toString().isEmpty()){*error=QStringLiteral("API恢复检查点无效。");return false;}
        auto binding=s->agent.value("profileBindings").toMap().value(profile).toMap();binding.insert("responseId",e["responseId"].toString());binding.insert("apiCwd",it->snapshot["cwd"].toString());updateProfileBinding(*s,profile,binding);
        m_profiles.usage(profile,it->id,e["usage"].toObject());
        it->usage=e["usage"].toObject();
        save();return true;
    }
    if (kind == "end") {
        end(session, e["state"].toInt(int(State::Interrupted)), e["detail"].toString());
        return true;
    }
    if (kind == "mailReceipt") {
        if (e["turnId"].toString() == it->turn && e["mailId"].toString() == it->mail)
            settleMail(session, it->id, it->mail, e["state"].toInt());
        return true;
    }
    if (kind == "approvalClosed") {
        it->approvals.removeIf(
            [&](const QVariant &v) { return v.toMap().value("token").toString() == e["token"].toString(); });
        emit changed();
        return true;
    }
    if (kind == "approval") {
        if (it->stopping) return true;
        it->approvals.append(e.toVariantMap());
        emit approvalArrived(session, it->id, e["token"].toString(), s->title);
        emit changed();
        return true;
    }
    auto id = it->id;
    if (kind == "output" || kind == "itemEnd")
        id += "." + e["item"].toString();
    auto ix = std::find_if(s->messages.cbegin(), s->messages.cend(), [&](const auto &m) { return m.id == id; });
    ChatMessage msg = ix == s->messages.cend() ? ChatMessage {} : *ix;
    msg.id = id;
    msg.status = MessageStatus::Running;
    if (kind == "thread") {
        auto binding=s->agent.value("profileBindings").toMap().value(profile).toMap();
        binding.insert("identity",e["identity"].toString());binding.insert("threadId",e["threadId"].toString());binding.insert("boundAccount",it->snapshot["account"].toInt());binding.insert("boundCwd",it->snapshot["cwd"].toString());updateProfileBinding(*s,profile,binding);
        msg.activity.insert("identity", e["identity"].toString());
        msg.activity.insert("threadId", e["threadId"].toString());
        msg.activity.insert("model", e["model"].toString());
    } else if (kind == "turn") {
        it->turn = e["turnId"].toString();
        msg.activity.insert("turnId", it->turn);
        msg.activity.insert("taskState", int(State::Running));
        msg.content = it->snapshot.contains("engine") ? it->snapshot["engine"].toString()+QStringLiteral(" 正在执行") : it->snapshot["account"].toInt()==agent::OpenAIResponses ? QStringLiteral("OpenAI API 正在执行") : QStringLiteral("Codex 正在执行");
    } else if (kind == "reasoningPhase") {
        const auto phase = e["state"].toInt(-1);
        if (phase != int(State::Running) && phase != int(State::Complete)) {
            *error = QStringLiteral("未知的思考阶段状态。");
            return false;
        }
        msg.content = phase == int(State::Running) ? QStringLiteral("正在思考") : it->snapshot.contains("engine") ? it->snapshot["engine"].toString()+QStringLiteral(" 正在执行") : QStringLiteral("Codex 正在执行");
    } else if (kind == "itemEnd") {
        if (ix == s->messages.cend())
            return true;
        msg.status = messageStatus(e["state"].toInt());
        msg.activity.insert("taskState", e["state"].toInt());
        // 对账会重放完成事件，原证明必须随这次权威结果一并替换。
        msg.activity.remove("filePatches");
        msg.activity.remove("patchWorkspace");
        if (e["filePatches"].isObject() && msg.activity.value("blockKind").toInt() == int(content::BlockKind::Diff)
            && e["state"].toInt() == int(State::Complete)) {
            msg.activity.insert("filePatches", e["filePatches"].toObject().toVariantMap());
            for (const auto &marker : std::as_const(s->messages)) {
                if (marker.id != it->id) continue;
                msg.activity.insert("patchWorkspace", marker.activity.value("cwd"));
                break;
            }
        }
        const auto readPath = e["readPath"].toString();
        if (!readPath.isEmpty()) {
            msg.activity.insert("readPath", readPath);
            msg.activity.insert("summary", QStringLiteral("已读取 %1").arg(QFileInfo(readPath).fileName()));
        }
        if (e["exitCode"].isDouble())
            msg.activity.insert("exitCode", e["exitCode"].toInt());
    } else if (kind == "output") {
        msg.role = e["type"] == "assistant" ? MessageRole::Assistant : MessageRole::Tool;
        if (!e["title"].toString().isEmpty())
            msg.title = e["title"].toString();
        if (msg.title.isEmpty() && msg.role == MessageRole::Tool)
            msg.title = QStringLiteral("命令输出");
        msg.activity.insert("serviceId", "codex-agent");
        msg.activity.insert("runId", it->id);
        if (msg.role == MessageRole::Tool) {
            msg.activity.insert("kind", 2);
            msg.activity.insert("summary", msg.title);
            msg.activity.insert(
                "blockKind", int(e["type"] == "diff" ? content::BlockKind::Diff : content::BlockKind::Terminal));
        }
        if (e["replace"].toBool())
            msg.content = e["text"].toString();
        else
            msg.content += e["text"].toString();
    } else {
        *error = QStringLiteral("未知的 Agent 事件。");
        return false;
    }
    put(*s, msg);
    if (kind != "output")
        save();
    if (kind == "turn") deliverMail(session);
    return true;
}
}
