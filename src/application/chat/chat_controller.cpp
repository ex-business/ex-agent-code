#include "chat_controller.h"
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
    m_exporter.setMaxThreadCount(1);
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
    QString error;
    m_sessions = m_repository.load(&error);
    m_storageWritable = error.isEmpty();
    // 旧进程已不再存活；只修正本服务创建的运行记录，不改导入资源和其他工具历史。
    for (auto &session : m_sessions) for (auto &message : session.messages) {
        const auto serviceId = message.activity.value("serviceId").toString();
        if ((serviceId != "local-service" && serviceId != "internal-analysis") || message.status != MessageStatus::Running) continue;
        message.status = MessageStatus::Failed;
        message.activity.insert("taskState", int(ipc::TaskState::Interrupted));
        message.activity.insert("summary", message.title + QStringLiteral(" · 结果待确认"));
        message.content += QStringLiteral("\n应用已重新启动，原后台任务不自动恢复，已有内容保留。\n");
    }
    if (!error.isEmpty()) setError(error);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatController::flush);
    connect(&m_messages, &MessageListModel::pageFailure, this, &ChatController::setError);
    connect(&m_messages, &MessageListModel::contentUpdated, this, &ChatController::persistCurrent);
    connect(&m_messages, &MessageListModel::presentationChanged, this, &ChatController::persistCurrent);
    if (m_sessions.isEmpty()) newSession();
    else selectSession(m_sessions.first().id);
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
    if (m_preview) {
        if (m_previewDraft != text) { m_previewDraft = text; emit draftChanged(); }
        return;
    }
    if (m_current < 0 || text == draft()) return;
    m_sessions[m_current].draft = text;
    emit draftChanged();
    m_saveTimer.start();
}

void ChatController::newSession()
{
    ChatSession session;
    session.id = createId();
    session.title = QStringLiteral("新聊天");
    m_sessions.prepend(session);
    m_current = 0;
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
    if (m_current < 0 || text.isEmpty()) return false;
    if (text.size() > 64 * 1024) {
        setError(QStringLiteral("单条消息不能超过 64K 字符，请拆分后发送。"));
        return false;
    }
    if (m_preview) {
        // 预览输入只创建新的本地会话，样例不混入真实聊天历史。
        newSession();
    }
    if (m_messages.count() == 0) {
        m_sessions[m_current].title = text.simplified().left(32);
        emit sessionChanged();
    }
    m_sessions[m_current].draft.clear();
    emit draftChanged();
    ChatMessage message;
    message.id = createId();
    message.content = text;
    m_messages.append(message);
    // 当前只交付聊天界面，明确提示未连接引擎，不生成伪造的助手回复。
    ChatMessage notice;
    notice.id = createId();
    notice.role = MessageRole::Notice;
    notice.content = QStringLiteral("消息已保存在本地。Agent 引擎尚未连接，当前不会调用模型或执行工具。");
    m_messages.append(notice);
    flush();
    return true;
}

QVariantList ChatController::resourceConversations() const { return m_resources.list(); }
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
void ChatController::openImageSequence(const QUrl &folder)
{
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

void ChatController::refreshSessions() { m_sessionModel.replace(m_sessions); }
void ChatController::setError(const QString &message)
{
    if (message == m_error) return;
    m_error = message;
    if (!message.isEmpty()) qCWarning(chatLog).noquote() << message;
    emit errorChanged();
}
void ChatController::flush()
{
    m_saveTimer.stop();
    if (!m_storageWritable) return;
    QString error;
    if (!m_repository.save(m_sessions, &error)) setError(error);
}

} // namespace exagent
