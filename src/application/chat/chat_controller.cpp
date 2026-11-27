#include "chat_controller.h"
#include "plugins/session_history/terminal_backend.h"
#include <QSettings>
#include "plugins/design_mode/design_selection.h"
#include "infrastructure/storage/conversation_image.h"
#include "application/services/media_file_exporter.h"

#include <QClipboard>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUuid>
#include <algorithm>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtConcurrentRun>

Q_LOGGING_CATEGORY(chatLog, "exagent.chat")

namespace exagent {

ChatController::ChatController(QObject *parent)
    : QObject(parent), m_messages(this), m_sessionModel(this),
      m_repository(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)), m_service(this), m_blocks(this), m_blockViews(m_blocks, this), m_externalBlocks(m_blocks), m_internalTask(m_blocks, this), m_mediaCatalog(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), this), m_mediaTask(m_blocks, m_mediaCatalog, this)
{
    connect(this, &ChatController::sessionChanged, this, &ChatController::designSelectionChanged);
    connect(this, &ChatController::workspaceChanged, this, &ChatController::reviewNotesChanged);
    connect(this,&ChatController::sessionChanged,this,[this] {
        m_agent.select(m_preview ? QString{} : currentSessionId(),m_storageWritable && !m_preview);
    });
    connect(&m_agent,&AgentController::failure,this,&ChatController::setError);
    connect(&m_agent,&AgentController::saved,this,[this] {
        refreshSessions(); emit sessionChanged(); emit draftChanged();
    });
    connect(&m_agent,&AgentController::rowChanged,this,[this](const QString &session,const ChatMessage &row) {
        if (!m_preview && currentSessionId() == session) {
            m_messages.upsertServiceMessage(row);
            if (row.status != MessageStatus::Running
                && row.activity.value("blockKind").toInt() == int(content::BlockKind::Diff))
                emit reviewEvidenceChanged();
        }
        // 事件按会话写入；后台输出同样需要在空闲时保存。
        m_saveTimer.start();
    });
    m_exporter.setMaxThreadCount(1);
    connect(this, &ChatController::sessionChanged, this, &ChatController::workspaceChanged);
    m_service.setEventSink([this](const QString &method, const QJsonObject &event, QString *error) { return m_externalBlocks.apply(method, event, error); });
    m_blockUpdateTimer.setSingleShot(true); m_blockUpdateTimer.setInterval(16);
    connect(&m_blockUpdateTimer, &QTimer::timeout, this, &ChatController::drainServiceBlocks);
    connect(&m_blocks, &ContentBlockStore::blockChanged, this, [this](const QString &id) {
        const auto *block = m_blocks.find(id);
        if (!block || block->mode == content::ContentMode::Static) return;
        // 协议逐事件归约，界面每16ms按块合并刷新，不让输出频率决定整段排版频率。
        if (!m_dirtyBlocks.contains(id)) m_dirtyBlocks.append(id);
        if (!m_blockUpdateTimer.isActive()) m_blockUpdateTimer.start();
    });
    connect(&m_service, &LocalServiceProcess::stateChanged, this, &ChatController::serviceStateChanged);
    connect(&m_service, &LocalServiceProcess::ready, this, &ChatController::dispatchBackgroundTasks);
    connect(&m_service, &LocalServiceProcess::failure, this, [this](const QString &reason) {
        m_waitingTasks.clear(); m_taskRequests.clear(); m_blocks.interrupt(reason); setError(reason);
    });
    connect(&m_service, &LocalServiceProcess::response, this, [this](const QString &id, const QJsonValue &) { m_taskRequests.remove(id); });
    connect(&m_service, &LocalServiceProcess::requestFailed, this, [this](const QString &id, int code, const QString &reason) {
        const auto taskId = m_taskRequests.take(id);
        if (!taskId.isEmpty()) m_blocks.interrupt(reason, taskId, code == -32001 || code == -32002 ? ipc::TaskState::Interrupted : ipc::TaskState::Failed);
        setError(reason);
    });
    connect(&m_mediaTask, &MediaContentTask::failure, this, &ChatController::setError);
    connect(&m_mediaCatalog, &MediaCatalog::failure, this, &ChatController::setError);
    connect(&m_blockViews, &BlockViewRegistry::failure, this, &ChatController::setError);
    connect(&m_internalTask, &InternalContentTask::failure, this, &ChatController::setError);
    connect(&m_transfer, &ConversationTransfer::failure, this, &ChatController::setError);
    connect(&m_transfer, &ConversationTransfer::published, this, [this](const QString &selected) {
        if (!m_preview) selectSession(selected);
        else {
            // 异步复制期间用户已进入只读历史。新会话插入首位后重新定位后台会话下标。
            for (int i = 0; i < m_sessions.size(); ++i)
                if (m_sessions[i].id == selected) { m_current = i; break; }
            refreshSessions();
        }
    });
    QString error;
    m_sessions = m_repository.load(m_projects.catalog, &error);
    m_agent.indexWorkspaceActivity();
    m_storageWritable = error.isEmpty();
    m_projects.setWritable(m_storageWritable);
    m_agent.workspaceAvailable = [this](const ChatSession &s) { return m_projects.available(s.projectId,s.workspaceId); };
    m_projects.runningTasks = [this](const QString &path) {
        QStringList active;
        for (const auto &s : m_sessions) {
            const auto *w = m_projects.catalog.workspace(s.projectId, s.workspaceId);
            if (!w || w->path != path) continue;
            const auto running = [this](const QString &id) {
                const auto state = m_blocks.taskState(id);
                return state == int(content::TaskState::Created) || state == int(content::TaskState::Running);
            };
            if (m_agent.activeSession(s.id) || running(m_backgroundTasks.value(s.id)) || running(m_internalTasks.value(s.id)))
                active.append(s.title + " · " + s.id);
        }
        // 终端可能在后台或其他窗口存活，核对持久绑定及全部分屏，未知后端状态也阻止清理。
        const auto tabs = QSettings().value("terminal/tabs").toList();
        for (const auto &value : tabs) {
            const auto tab = value.toMap();
            const auto cwd = tab.value("cwd").toString();
            if (cwd != path && !cwd.startsWith(path + '/')) continue;
            const auto panes = terminalCommand({"list-panes", "-s", "-t", "=" + tab.value("id").toString(), "-F", "#{pane_dead}"});
            if (!panes.ok || panes.output.split('\n').contains("0"))
                active.append(QStringLiteral("终端仍在使用或存活状态待核对 · ") + tab.value("title").toString());
        }
        return active;
    };
    m_projects.saveCatalog = [this] {
        QString error;
        if (m_repository.save(m_projects.catalog, m_sessions, &error)) return true;
        setError(error);
        return false;
    };
    connect(&m_projects, &ProjectNavigator::catalogChanged, this, [this] { refreshSessions(); emit workspaceChanged(); });
    connect(&m_projects, &ProjectNavigator::openGroup, this, &ChatController::openGroup);
    connect(&m_projects, &ProjectNavigator::failure, this, &ChatController::setError);
    // 仅持有仓库写锁的宿主执行重启恢复；只读窗口看到的任务可能仍由另一实例运行。
    if (m_storageWritable) {
        m_agent.restore();
        for (auto &session : m_sessions) for (auto &message : session.messages) {
            const auto serviceId = message.activity.value("serviceId").toString();
            if ((serviceId != "local-service" && serviceId != "internal-analysis") || message.status != MessageStatus::Running) continue;
            message.status = MessageStatus::Failed;
            message.activity.insert("taskState", int(ipc::TaskState::Interrupted));
            message.activity.insert("summary", message.title + QStringLiteral(" · 结果待确认"));
            message.content += QStringLiteral("\n应用已重新启动，原后台任务不自动恢复，已有内容保留。\n");
        }
    }
    if (!error.isEmpty()) setError(error);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatController::flush);
    connect(&m_messages, &MessageListModel::pageFailure, this, &ChatController::setError);
    connect(&m_messages, &MessageListModel::contentUpdated, this, &ChatController::persistCurrent);
    connect(&m_messages, &MessageListModel::presentationChanged, this, &ChatController::persistCurrent);
    const auto previousResource = m_projects.catalog.selectedResource;
    const bool firstOpen = m_sessions.isEmpty();
    if (firstOpen) newSession();
    else {
        const auto selected = m_projects.catalog.selectedSession;
        selectSession(selected);
        if (m_current < 0) selectSession(m_sessions.first().id);
    }
    if (!previousResource.isEmpty()) loadResourceConversation(previousResource);
    else if (firstOpen) {
        for (const auto &entry : resourceConversations())
            if (entry.toMap().value("id") == "webchat-go") { loadResourceConversation("webchat-go"); break; }
    }
}

void ChatController::setBots(QObject *value)
{
    m_bots = value;
    if (value) connect(value, &QObject::destroyed, this, [this] { if (!m_bots) emit botsChanged(); });
    emit botsChanged();
}

void ChatController::setPlugins(QObject *value)
{
    m_plugins = value;
    if (value) connect(value, &QObject::destroyed, this, [this] { if (!m_plugins) emit pluginsChanged(); });
    emit pluginsChanged();
}

ChatController::~ChatController()
{
    // 先断开后台回调再销毁成员，避免等待子进程退出时访问已销毁的内容仓库。
    disconnect(&m_service, nullptr, this, nullptr);
    m_service.setEventSink({});
    m_mediaTask.stopAll();
    m_internalTask.stopAll();
    m_blocks.interrupt(QStringLiteral("应用正在退出，后台任务不自动重新执行"));
    drainServiceBlocks();
    flush();
    disconnect(&m_blocks, nullptr, this, nullptr);
    m_service.stop();
}

MessageListModel *ChatController::messages() { return &m_messages; }
SessionListModel *ChatController::sessions() { return &m_sessionModel; }
QString ChatController::currentSessionId() const { return m_current < 0 ? QString() : m_sessions[m_current].id; }
QString ChatController::sessionTitle() const { return m_preview ? m_previewTitle : m_current < 0 ? QStringLiteral("新聊天") : m_sessions[m_current].title; }
QString ChatController::draft() const { return m_preview ? m_previewDraft : m_current < 0 ? QString() : m_sessions[m_current].draft; }
QString ChatController::errorMessage() const { return m_error; }
QString ChatController::storagePath() const { return m_repository.filePath(); }
bool ChatController::previewMode() const { return m_preview; }
QString ChatController::createId() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

void ChatController::setDraft(const QString &text)
{
    if (!m_storageWritable) return;
    if (m_preview) {
        if (m_previewDraft != text) { m_previewDraft = text; emit draftChanged(); }
        return;
    }
    if (m_current < 0 || text == draft()) return;
    m_sessions[m_current].draft = text;
    emit draftChanged();
    m_saveTimer.start();
}

QString ChatController::projectName() const
{
    const auto *project = m_projects.catalog.find(currentProjectId());
    return project ? project->name : QString{};
}

QString ChatController::workspacePath() const
{
    if (m_current < 0 || m_preview) return {};
    const auto &session = m_sessions[m_current];
    const auto *workspace = m_projects.catalog.workspace(session.projectId, session.workspaceId);
    return workspace ? workspace->path : QString{};
}

void ChatController::openGroup(const QString &project, const QString &workspace)
{
    for (const auto &session : m_sessions) {
        if (session.projectId == project && session.workspaceId == workspace) {
            selectSession(session.id);
            return;
        }
    }
    newGroupSession(project, workspace);
}

bool ChatController::moveToGroup(const QString &project, const QString &workspace)
{
    if (m_preview || m_current < 0 || !m_storageWritable) return false;
    auto &session = m_sessions[m_current];
    if (session.projectId != project || (!workspace.isEmpty() && !m_projects.catalog.workspace(project, workspace))) return false;
    if (!m_projects.available(project,workspace) || (!workspacePath().isEmpty() && !m_projects.runningTasks(workspacePath()).isEmpty()))
        return false;
    session.workspaceId = workspace;
    refreshSessions();
    // 只更新导航和文件区，移动会话时保留正文的阅读位置和选区。
    emit workspaceChanged();
    m_saveTimer.start();
    return true;
}

void ChatController::newSession()
{
    if (m_current >= 0 && !m_preview) {
        const auto session = m_sessions.at(m_current);
        newGroupSession(session.projectId, session.workspaceId);
    } else newGroupSession(ProjectCatalog::inboxId(), {});
}

void ChatController::newGroupSession(const QString &project, const QString &workspace)
{
    if (!m_projects.catalog.find(project) || (!workspace.isEmpty() && !m_projects.catalog.workspace(project, workspace))) return;
    if (!m_storageWritable && !m_sessions.isEmpty()) { setError(QStringLiteral("项目目录只读，无法新建聊天。")); return; }
    ChatSession session;
    session.projectId = project;
    session.workspaceId = workspace;
    session.id = createId();
    session.title = QStringLiteral("新聊天");
    m_sessions.prepend(session);
    m_current = 0;
    m_projects.catalog.selectedSession = session.id;
    m_projects.catalog.selectedResource.clear();
    m_preview = false;
    m_messages.setContentContext(session.id, &m_blocks);
    m_messages.replace({});
    refreshSessions();
    emit sessionChanged();
    emit backgroundTaskChanged();
    emit draftChanged();
    m_saveTimer.start();
}

void ChatController::selectSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id != id) continue;
        m_current = i;
        m_projects.catalog.selectedSession = id;
        m_projects.catalog.selectedResource.clear();
        m_saveTimer.start();
        m_preview = false;
        m_messages.setContentContext(id, &m_blocks);
        m_messages.replace(m_sessions[i].messages);
        refreshSessions();
        emit sessionChanged();
        emit backgroundTaskChanged();
        emit draftChanged();
        return;
    }
}

bool ChatController::sendMessage()
{
    const auto text = draft().trimmed();
    if (!m_storageWritable) { setError(QStringLiteral("项目目录只读，无法保存消息。")); return false; }
    if (m_current < 0 || text.isEmpty()) return false;
    if (designCaptureBusy()) { setError(QStringLiteral("正在准备页面截图，请稍后发送。")); return false; }
    if (text.size() > 64 * 1024) {
        setError(QStringLiteral("单条消息不能超过 64K 字符，请拆分后发送。"));
        return false;
    }
    if (!m_preview && m_agent.account()) return m_agent.submit(text,workspacePath());
    if (m_preview) {
        // 预览输入只创建新的本地会话，样例不混入真实聊天历史。
        newSession();
    }
    ChatMessage message;
    QString imagePath;
    auto &session = m_sessions[m_current];
    const auto error = design::apply(design::pending(session), m_repository.sessionDirectory(session.projectId,session.id), message, imagePath);
    if (!error.isEmpty()) { setError(error); return false; }
    design::setPending(session, {});
    emit designSelectionChanged();
    if (m_messages.count() == 0) {
        m_sessions[m_current].title = text.simplified().left(32);
        emit sessionChanged();
    }
    m_sessions[m_current].draft.clear();
    emit draftChanged();
    message.id = createId();
    message.content = text;
    m_messages.append(message);
    // 仅本地模式只保存本条聊天；用户选择执行账号后由 Agent 模块处理派发。
    ChatMessage notice;
    notice.id = createId();
    notice.role = MessageRole::Notice;
    notice.content = QStringLiteral("消息已保存在本地。可在会话执行设置中选择 Codex。");
    m_messages.append(notice);
    flush();
    return true;
}

QVariantList ChatController::resourceConversations() const
{
    // 已转为可写会话的历史只显示在项目树内，老版未迁移的资源仍可直接预览。
    QSet<QString> imported;
    for (const auto &session : m_sessions) imported.insert(session.history.value("resourceId").toString());
    auto entries = m_resources.list();
    entries.removeIf([&imported](const QVariant &entry) { return imported.contains(entry.toMap().value("id").toString()); });
    return entries;
}
QVariantList ChatController::navigationEntries() const { return m_resources.navigation(); }
QVariantMap ChatController::navigationInfo() const { return m_resources.navigationInfo(); }
QString ChatController::resourcePath() const { return m_resources.rootPath(); }
void ChatController::showPreview() { loadResourceConversation("demo-basic"); }
void ChatController::showLongPreview() { loadResourceConversation("demo-code"); }
void ChatController::showLargeChatPreview() { loadResourceConversation("demo-long-chat"); }
void ChatController::showConversationPreview() { loadResourceConversation("qt-discussion"); }

void ChatController::loadResourceConversation(const QString &id)
{
    QString error;
    const auto resource = m_resources.manifest(id, &error);
    if (!error.isEmpty()) { setError(error); return; }
    auto latest = m_resources.page(resource, resource.pageCount - 1, &error);
    if (!error.isEmpty()) { setError(error); return; }
    // 导入的历史是只读资源，不写回原始 Codex 会话，也不覆盖本地聊天。
    m_preview = true;
    m_projects.catalog.selectedResource = id;
    m_saveTimer.start();
    m_previewTitle = resource.title;
    m_changeSummary = resource.changeSummary;
    m_activityStatus = resource.activityStatus;
    m_previewDraft.clear();
    m_messages.setContentContext(QStringLiteral("resource.") + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()), &m_blocks);
    m_messages.replacePaged(std::move(latest), resource.messageCount, resource.pageCount - 2,
        [repository = m_resources, resource](int number) {
            MessageListModel::OlderPage result;
            result.messages = repository.page(resource, number, &result.error);
            return result;
        });
    emit sessionChanged();
    emit backgroundTaskChanged();
    emit draftChanged();
}

int ChatController::backgroundTaskState() const
{
    return m_blocks.taskState(backgroundTaskId());
}
void ChatController::runBackgroundTask()
{
    if (!m_projects.available(currentProjectId(),currentWorkspaceId())) { setError(QStringLiteral("工作区当前不可执行任务。")); return; }
    if (backgroundTaskState() == int(ipc::TaskState::Created) || backgroundTaskState() == int(ipc::TaskState::Running)) return;
    if (m_service.state() == ipc::ServiceState::Failed && m_service.processId() > 0) {
        setError(QStringLiteral("后台进程正在结束，请稍后重试。")); return;
    }
    if (m_waitingTasks.size() >= 16) { setError(QStringLiteral("待派发后台任务已满，请稍后重试。")); return; }
    if (m_preview) newSession();
    if (m_current < 0 || !m_storageWritable) { setError(QStringLiteral("会话无法保存，未启动后台任务。")); return; }
    if (m_sessions[m_current].messages.isEmpty()) {
        m_sessions[m_current].title = QStringLiteral("后台通信任务");
        emit sessionChanged();
    }
    ContentBlock block;
    block.id = createId(); block.taskId = createId(); block.conversationId = currentSessionId();
    block.title = QStringLiteral("后台通信任务"); block.text = QStringLiteral("等待独立后台进程接收任务。\n");
    QString error;
    if (!m_blocks.prepare(block, &error)) { setError(error); return; }
    m_backgroundTasks.insert(block.conversationId, block.taskId);
    m_waitingTasks.append({{"conversationId", block.conversationId}, {"taskId", block.taskId}, {"blockId", block.id}, {"kind", 1}, {"title", block.title}});
    emit backgroundTaskChanged();
    if (m_service.state() == ipc::ServiceState::Ready) dispatchBackgroundTasks();
    else m_service.start();
}
void ChatController::dispatchBackgroundTasks()
{
    const auto tasks = m_waitingTasks; m_waitingTasks.clear();
    for (const auto &task : tasks) {
        const auto taskId = task.value("taskId").toString();
        const auto id = m_service.request("task.start", task);
        if (id.isEmpty()) m_blocks.interrupt(QStringLiteral("后台请求队列未接受任务"), taskId, ipc::TaskState::Failed);
        else m_taskRequests.insert(id, taskId);
    }
}
void ChatController::cancelBackgroundTask()
{
    const auto taskId = backgroundTaskId();
    if (taskId.isEmpty()) return;
    // 尚未派发的任务直接在宿主取消，不因点击取消而额外启动工作进程。
    for (int i = 0; i < m_waitingTasks.size(); ++i) if (m_waitingTasks[i].value("taskId").toString() == taskId) {
        m_waitingTasks.removeAt(i); m_blocks.interrupt(QStringLiteral("任务在派发前被用户取消"), taskId, ipc::TaskState::Cancelled); return;
    }
    const auto id = m_service.request("task.cancel", {{"taskId", taskId}});
    if (id.isEmpty()) setError(QStringLiteral("后台暂不可达，无法确认取消结果；不会自动重跑任务。"));
    else m_taskRequests.insert(id, taskId);
}
int ChatController::internalTaskState() const
{
    if (m_preview) return -1;
    return m_blocks.taskState(m_internalTasks.value(currentSessionId()));
}
void ChatController::analyzeLoadedConversation()
{
    if (!m_projects.available(currentProjectId(),currentWorkspaceId())) { setError(QStringLiteral("工作区当前不可执行任务。")); return; }
    if (internalTaskState() == int(content::TaskState::Created) || internalTaskState() == int(content::TaskState::Running)) return;
    // 只复制已加载消息的共享字符串快照，统计本身在线程池执行，不读取额外历史页。
    QStringList texts;
    for (int row = 0; row < m_messages.count(); ++row) texts.append(m_messages.data(m_messages.index(row), MessageListModel::ContentRole).toString());
    const auto sourceTitle = sessionTitle();
    if (m_preview) newSession();
    if (m_current < 0 || !m_storageWritable) { setError(QStringLiteral("会话无法保存，未启动内部任务。")); return; }
    if (m_sessions[m_current].messages.isEmpty()) {
        m_sessions[m_current].title = QStringLiteral("会话内容统计"); emit sessionChanged();
    }
    ContentBlock block;
    block.id = createId(); block.taskId = createId(); block.conversationId = currentSessionId();
    block.title = QStringLiteral("已加载会话内容统计");
    const auto conversationId = block.conversationId, taskId = block.taskId;
    QString error;
    if (!m_internalTask.start(std::move(block), std::move(texts), sourceTitle, &error)) { setError(error); return; }
    m_internalTasks.insert(conversationId, taskId); emit backgroundTaskChanged();
}
void ChatController::cancelInternalTask()
{
    if (!m_preview) {
        const auto id = m_internalTasks.value(currentSessionId()); m_internalTask.cancel(id); m_mediaTask.cancel(id);
    }
}
void ChatController::exportBlock(const QVariantMap &block, const QUrl &target)
{
    if (!target.isLocalFile() || m_exportPending > 0) { setError(QStringLiteral("请选择本机导出位置，并等待已有导出任务完成")); return; }
    auto snapshot = m_blocks.snapshot(block.value("blockId").toString());
    if (snapshot.isEmpty()) snapshot = block;
    ++m_exportPending;
    auto *watcher = new QFutureWatcher<bool>(this);
    const auto path = target.toLocalFile();
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, path] {
        --m_exportPending;
        bool success = false;
        try { success = watcher->result(); } catch (...) {}
        if (success) emit blockExported(path);
        else setError(QStringLiteral("内容导出失败，原始内容已保留"));
        watcher->deleteLater();
    });
    // 用户选择目标后才写文件，序列化和落盘均离开GUI线程；导出不改变块或任务状态。
    watcher->setFuture(QtConcurrent::run(&m_exporter, [snapshot, path] {
        const auto bytes = QJsonDocument::fromVariant(snapshot).toJson();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return file.write(bytes) == bytes.size() && file.commit();
    }));
}
void ChatController::attachImage(const QUrl &file)
{
    if (!m_storageWritable || !file.isLocalFile() || m_exportPending) {
        setError(QStringLiteral("请选择本地图片，并等待当前文件操作完成。"));
        return;
    }
    if (m_preview) newSession();
    if (m_current < 0) return;
    const auto id = currentSessionId();
    const auto directory = m_repository.sessionDirectory(currentProjectId(), id);
    ++m_exportPending;
    auto *watcher = new QFutureWatcher<ImageImport>(this);
    connect(watcher, &QFutureWatcher<ImageImport>::finished, this, [this, watcher, id] {
        --m_exportPending;
        const auto imported = watcher->result();
        watcher->deleteLater();
        if (!imported.error.isEmpty()) { setError(imported.error); return; }
        // 导入完成时用户可能已切换聊天；始终按选择图片时的会话身份发布。
        for (auto &session : m_sessions) {
            if (session.id != id) continue;
            ChatMessage image;
            image.id = createId();
            image.role = MessageRole::Tool;
            image.title = QStringLiteral("图片");
            image.activity = {{"kind", 3}, {"summary", imported.attachment.value("name")}};
            image.expanded = true;
            image.attachments = {imported.attachment};
            session.messages.append(image);
            session.updatedAt = QDateTime::currentDateTimeUtc();
            if (!m_preview && currentSessionId() == id) m_messages.append(image);
            refreshSessions();
            flush();
            break;
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_exporter, [path = file.toLocalFile(), directory] {
        return importConversationImage(path, directory, QFileInfo(path).fileName());
    }));
}

void ChatController::openImageSequence(const QUrl &folder)
{
    if (!m_projects.available(currentProjectId(),currentWorkspaceId())) { setError(QStringLiteral("工作区当前不可执行任务。")); return; }
    if (!folder.isLocalFile()) { setError(QStringLiteral("请选择本机图片目录")); return; }
    if (internalTaskState() == 0 || internalTaskState() == 1) { setError(QStringLiteral("请先等待或取消当前内部任务")); return; }
    if (m_preview) newSession();
    if (m_current < 0 || !m_storageWritable) { setError(QStringLiteral("会话无法保存，未加载图片")); return; }
    if (m_sessions[m_current].messages.isEmpty()) { m_sessions[m_current].title = QStringLiteral("图片序列"); emit sessionChanged(); }
    ContentBlock block; block.id = createId(); block.taskId = createId(); block.conversationId = currentSessionId();
    const auto taskId = block.taskId, conversation = block.conversationId;
    QString error;
    if (!m_mediaTask.start(std::move(block), folder.toLocalFile(), &error)) { setError(error); return; }
    m_internalTasks.insert(conversation, taskId); emit backgroundTaskChanged();
}
void ChatController::saveMedia(const QVariantMap &block, const QString &resourceId, const QUrl &attachment, const QUrl &target)
{
    QUrl source;
    if (!attachment.isEmpty()) {
        for (const auto &message : m_messages.messages()) for (const auto &entry : message.attachments)
            if (QUrl(entry.toMap().value("url").toString()) == attachment) source = attachment;
    } else source = mediaSource(block, resourceId);
    const auto path = source.toLocalFile();
    if (!source.isLocalFile() || !target.isLocalFile() || path.isEmpty() || m_exportPending > 0
        || QFileInfo(path).canonicalFilePath() != path || QFileInfo(target.toLocalFile()).absoluteFilePath() == path
        || QFileInfo(target.toLocalFile()).canonicalFilePath() == path || block.value("sourceKind").toInt() == int(content::BlockSource::External)) {
        setError(QStringLiteral("图片未授权、目标不可用或已有导出正在进行")); return;
    }
    ++m_exportPending;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, savedPath = target.toLocalFile()] {
        --m_exportPending;
        QString error;
        try { error = watcher->result(); } catch (...) { error = QStringLiteral("后台复制发生异常"); }
        if (!error.isEmpty()) {
            qWarning().noquote() << "原始图片保存失败，目标：" << savedPath << "原因：" << error;
            setError(QStringLiteral("原始图片保存失败：") + error);
        } else {
            qInfo().noquote() << "原始图片已保存，目标：" << savedPath;
            emit mediaSaved(savedPath);
        }
        watcher->deleteLater();
    });
    // 复制原始字节而非缩小纹理，耗时IO交给独立后台模块。
    watcher->setFuture(QtConcurrent::run(&m_exporter, [path, destination = target.toLocalFile()] {
        return MediaFileExporter::copyOriginal(path, destination);
    }));
}
QUrl ChatController::mediaSource(const QVariantMap &view, const QString &resourceId) const
{
    const auto id = view.value("blockId").toString();
    const auto current = m_blocks.snapshot(id);
    const auto block = current.isEmpty() ? view : current;
    // 外部协议尚未协商资源授权，不允许外部事件借已知资源ID读取宿主文件。
    if (block.value("sourceKind").toInt() == int(content::BlockSource::External)) return {};
    const auto data = block.value("data").toMap();
    bool allowed = data.value("resourceId").toString() == resourceId;
    for (const auto &frame : data.value("frames").toList()) if (frame.toMap().value("resourceId").toString() == resourceId) { allowed = true; break; }
    return allowed ? m_mediaCatalog.source(resourceId) : QUrl{};
}
void ChatController::drainServiceBlocks()
{
    m_blockUpdateTimer.stop();
    const auto ids = m_dirtyBlocks; m_dirtyBlocks.clear();
    for (const auto &id : ids) applyServiceBlock(id);
}
void ChatController::applyServiceBlock(const QString &id)
{
    const auto *block = m_blocks.find(id);
    if (!block || block->mode != content::ContentMode::Dynamic) return;
    int sessionIndex = -1;
    for (int i = 0; i < m_sessions.size(); ++i) if (m_sessions[i].id == block->conversationId) { sessionIndex = i; break; }
    if (sessionIndex < 0) return;
    auto &session = m_sessions[sessionIndex];
    ChatMessage message;
    const auto found = std::find_if(session.messages.cbegin(), session.messages.cend(), [&](const ChatMessage &entry) { return entry.id == id; });
    if (found != session.messages.cend()) message = *found;
    message.id = block->id; message.role = MessageRole::Tool; message.title = block->title;
    message.content = block->text;
    // 外部任务状态显式映射，不能把完成2、失败3误当成已有消息状态。
    switch (block->state) {
    case ipc::TaskState::Complete: message.status = MessageStatus::Complete; break;
    case ipc::TaskState::Cancelled: message.status = MessageStatus::Cancelled; break;
    case ipc::TaskState::Failed:
    case ipc::TaskState::Interrupted: message.status = MessageStatus::Failed; break;
    default: message.status = MessageStatus::Running; break;
    }
    const auto stateLabel = block->state == ipc::TaskState::Created ? QStringLiteral("等待接收") : block->state == ipc::TaskState::Running ? QStringLiteral("运行中")
        : block->state == ipc::TaskState::Complete ? QStringLiteral("已完成") : block->state == ipc::TaskState::Cancelled ? QStringLiteral("已取消")
        : block->state == ipc::TaskState::Interrupted ? QStringLiteral("结果待确认") : QStringLiteral("未能执行");
    message.activity = {{"kind", 2}, {"summary", block->title + " · " + stateLabel}, {"serviceId", block->source == content::BlockSource::Internal ? "internal-analysis" : "local-service"}, {"taskId", block->taskId}, {"blockId", block->id},
        {"blockKind", int(block->kind)}, {"contentMode", int(block->mode)}, {"sourceKind", int(block->source)}, {"taskState", int(block->state)}, {"revision", block->revision}};
    if (!block->data.isEmpty()) message.activity.insert("data", block->data.toVariantMap());
    if (found == session.messages.cend()) { message.expanded = true; session.messages.append(message); }
    else session.messages[std::distance(session.messages.cbegin(), found)] = message;
    session.updatedAt = QDateTime::currentDateTimeUtc();
    if (!m_preview && m_current == sessionIndex) m_messages.upsertServiceMessage(message);
    else { refreshSessions(); m_saveTimer.start(); }
    emit backgroundTaskChanged();
}

void ChatController::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
void ChatController::clearError() { setError({}); }
void ChatController::setDarkTheme(bool dark)
{
    m_messages.setDark(dark);
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
}

void ChatController::persistCurrent()
{
    if (m_preview || m_current < 0) return;
    auto &session = m_sessions[m_current];
    session.messages = m_messages.messages();
    session.updatedAt = QDateTime::currentDateTimeUtc();
    refreshSessions();
    m_saveTimer.start();
}

void ChatController::refreshSessions() { m_sessionModel.replace(m_sessions); m_projects.refresh(m_sessions); }
void ChatController::setError(const QString &message)
{
    if (message == m_error) return;
    m_error = message;
    if (!message.isEmpty()) qCWarning(chatLog).noquote() << message;
    emit errorChanged();
}
bool ChatController::saveNow(QString *error)
{
    m_saveTimer.stop();
    if (!m_storageWritable) { *error = QStringLiteral("项目目录只读。"); return false; }
    return m_repository.save(m_projects.catalog, m_sessions, error);
}
void ChatController::flush()
{
    m_saveTimer.stop();
    if (!m_storageWritable) return;
    QString error;
    if (!saveNow(&error)) setError(error);
}

} // namespace exagent
