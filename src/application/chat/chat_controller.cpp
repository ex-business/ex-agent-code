#include "chat_controller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUuid>

Q_LOGGING_CATEGORY(chatLog, "exagent.chat")

namespace exagent {

ChatController::ChatController(QObject *parent)
    : QObject(parent), m_messages(this), m_sessionModel(this),
      m_repository(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
{
    QString error;
    m_sessions = m_repository.load(&error);
    m_storageWritable = error.isEmpty();
    if (!error.isEmpty()) setError(error);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatController::flush);
    connect(&m_messages, &MessageListModel::contentUpdated, this, &ChatController::persistCurrent);
    connect(&m_messages, &MessageListModel::presentationChanged, this, &ChatController::persistCurrent);
    if (m_sessions.isEmpty()) newSession();
    else selectSession(m_sessions.first().id);
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
    m_messages.replace({});
    refreshSessions();
    emit sessionChanged();
    emit draftChanged();
    m_saveTimer.start();
}

void ChatController::selectSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id != id) continue;
        m_current = i;
        m_preview = false;
        m_messages.replace(m_sessions[i].messages);
        refreshSessions();
        emit sessionChanged();
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
    m_previewDraft.clear();
    m_messages.replacePaged(std::move(latest), resource.messageCount,
        [this, resource, number = resource.pageCount - 2]() mutable {
            if (number < 0) return QList<ChatMessage>{};
            QString pageError;
            auto messages = m_resources.page(resource, number, &pageError);
            if (!pageError.isEmpty()) { setError(pageError); return QList<ChatMessage>{}; }
            --number;
            return messages;
        });
    emit sessionChanged();
    emit draftChanged();
}

void ChatController::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
void ChatController::clearError() { setError({}); }
void ChatController::setDarkTheme(bool dark)
{
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
