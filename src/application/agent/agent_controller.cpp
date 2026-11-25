#include "agent_controller.h"
#include "plugins/design_mode/design_selection.h"
#include "history_context.h"
#include "plugins/session_mail/mail.h"
#include "agent/codex/codex_account.h"
#include "agent/codex/thread_input.h"
#include <QDir>
#include <QJsonArray>
#include <QFileInfo>
#include <QUuid>
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
    , m_worker(ServiceLaunch { {}, { "--agent-worker" }, "codex-agent",
                   { "system.ping", "stream.resume", "agent.start", "agent.stop", "agent.approve", "agent.steer" } },
          this)
{
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
            emit failure(QStringLiteral("无法启动专用账号操作。"));
        }
    });
    connect(&m_login, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!code && status == QProcess::NormalExit) {
            codex::noteDedicatedLogin();
            emit accountUpdated(m_login.arguments().contains("login"));
        } else
            emit failure(QStringLiteral("专用账号操作失败，请检查 Codex CLI 和系统凭据存储。"));
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
    if (mailboxChanged) emit mailChanged();
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
void AgentController::configure(int account, const QString &model, bool write, bool fullAccess)
{
    auto *s = find(m_selected);
    if (!s || !m_writable || busy() || account < 0 || account > 2 || model.size() > 128)
        return;
    if (!codex::threadAvailable(s->agent.value("threadId").toString())) {
        emit failure(QStringLiteral("请先退出正在使用此线程的终端，再修改会话配置。"));
        return;
    }
    s->agent.insert("account", account);
    s->agent.insert("model", model.trimmed());
    s->agent.insert("write", write || fullAccess);
    s->agent.insert("fullAccess", fullAccess);
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
    if (!s || !m_writable || busy() || authenticating() || !account()
        || binding.value("boundAccount").toInt() != account() || binding.value("identity").toString().isEmpty()
        || QUuid(binding.value("threadId").toString()).isNull()) return {};
    const auto cwd = workspace.isEmpty() ? m_repository.sessionDirectory(s->projectId,s->id) + "/chat-cwd" : workspace;
    if (QFileInfo(cwd).canonicalFilePath() != binding.value("boundCwd").toString()) return {};
    QVariantMap spec;
    for (const auto& key : {"threadId","boundCwd","boundAccount","identity","model","write","fullAccess"})
        spec.insert(key,binding.value(key));
    spec.insert("conversationId",s->id);
    spec.insert("program", codex::executable());
    spec.insert("home", qEnvironmentVariable("CODEX_HOME", QDir::home().filePath(".codex")));
    return spec;
}
bool AgentController::submit(const QString &text, const QString &cwd, const QVariantMap &reviewNote)
{
    auto *s = find(m_selected);
    if (!s || !m_writable || !account() || text.trimmed().isEmpty())
        return false;
    if (!codex::threadAvailable(s->agent.value("threadId").toString())) {
        emit failure(QStringLiteral("会话正在另一入口中使用。请先在交互终端退出 Codex，再继续聊天。"));
        return false;
    }
    if (busy() || authenticating()) {
        emit failure(QStringLiteral("请等待当前运行或账号登录结束。"));
        return false;
    }
    QString directory;
    if (cwd.isEmpty()) {
        if (workspaceWrite()) {
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
    QJsonObject spec { { "conversationId", s->id }, { "runId", run }, { "prompt", text }, { "cwd", directory },
        { "model", model() }, { "write", workspaceWrite() }, { "fullAccess", fullAccess() }, { "account", account() } };
    if (s->agent.value("boundAccount").toInt() == account() && s->agent.value("boundCwd").toString() == directory)
        spec.insert("threadId", s->agent.value("threadId").toString());
    spec.insert("identity", s->agent.value("identity").toString());
    // 批注是独立输入；普通草稿选中的历史和图片只随该草稿派发。
    const auto ctx = reviewNote.isEmpty() ? m_contexts.value(s->id) : QVariantMap{};
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
    if (reviewNote.isEmpty()) {
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
    ChatMessage marker;
    marker.id = run;
    marker.role = MessageRole::Notice;
    marker.status = MessageStatus::Running;
    marker.content = QStringLiteral("Codex 正在准备执行");
    marker.activity = { { "serviceId", "codex-agent" }, { "taskState", int(State::Created) }, { "cwd", directory },
        { "account", account() }, { "requestedModel", model() } };
    // 用户输入与派发意图必须一起写入。失败时保留草稿，不能让 worker 收到半次派发。
    const auto before = *s;
    if (s->messages.isEmpty())
        s->title = text.simplified().left(32);
    if (!reviewNote.isEmpty()) user.activity = {{"reviewNote", reviewNote}, {"reviewRunId", run}};
    s->messages.append(user);
    s->messages.append(marker);
    if (reviewNote.isEmpty()) {
        design::setPending(*s, {});
        s->draft.clear();
    } else {
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
    if (reviewNote.isEmpty()) m_contexts.remove(s->id);
    m_latestMarker.insert(s->id, s->messages.size() - 1);
    m_active.insert(s->id, { run, spec, {}, false });
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
        m_active.insert(s->id, { m.id, spec, {}, false, {}, {}, true });
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
            m.content = detail;
            m.activity.insert("finishedAt", QDateTime::currentDateTimeUtc());
        }
        updates.append(m);
    }
    for (const auto &m : updates)
        emit rowChanged(session, m);
    save();
    emit changed();
    emit runEnded(session, a.id, state, s->title);
}
void AgentController::restore()
{
    // 宿主重启无法持有旧连接。保留线程与回合供后续恢复，不重放已发送的提示词。
    for (auto &s : m_sessions) recoverMail(s);
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
        s->agent.insert("identity", e["identity"].toString());
        msg.activity.insert("identity", e["identity"].toString());
        s->agent.insert("threadId", e["threadId"].toString());
        s->agent.insert("boundAccount", msg.activity.value("account"));
        s->agent.insert("boundCwd", msg.activity.value("cwd"));
        msg.activity.insert("threadId", e["threadId"].toString());
        msg.activity.insert("model", e["model"].toString());
    } else if (kind == "turn") {
        it->turn = e["turnId"].toString();
        msg.activity.insert("turnId", it->turn);
        msg.activity.insert("taskState", int(State::Running));
        msg.content = QStringLiteral("Codex 正在执行");
    } else if (kind == "reasoningPhase") {
        const auto phase = e["state"].toInt(-1);
        if (phase != int(State::Running) && phase != int(State::Complete)) {
            *error = QStringLiteral("未知的思考阶段状态。");
            return false;
        }
        msg.content = phase == int(State::Running) ? QStringLiteral("正在思考") : QStringLiteral("Codex 正在执行");
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
