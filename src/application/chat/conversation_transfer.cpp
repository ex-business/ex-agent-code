#include "conversation_transfer.h"
#include "infrastructure/storage/conversation_image.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QUrl>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>

namespace exagent {
namespace {
bool startsTurn(const ChatMessage &message)
{
    return message.startsTurn || message.role == MessageRole::User;
}

QList<int> turnStarts(const ChatSession &session)
{
    QList<int> result;
    // 老数据没有 turnId；以用户输入分界，开头的独立工具输出也保留为一个回合。
    for (int i = 0; i < session.messages.size(); ++i)
        if (i == 0 || startsTurn(session.messages[i])) result.append(i);
    return result;
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QByteArray sourceKey(const QString &conversation, const ChatMessage &message)
{
    const auto origin = message.originSession.isEmpty() ? conversation : message.originSession;
    const auto id = message.originMessage.isEmpty() ? message.id : message.originMessage;
    return QJsonDocument(QJsonArray{origin, id, message.activity.value("revision", 0).toInt()}).toJson(QJsonDocument::Compact);
}
}

ConversationTransfer::ConversationTransfer(QList<ChatSession> &sessions, ProjectCatalog &catalog, ProjectRepository &repository)
    : m_sessions(sessions), m_catalog(catalog), m_repository(repository) {}

const ChatSession *ConversationTransfer::find(const QString &id) const
{
    for (const auto &session : m_sessions)
        if (session.id == id) return &session;
    return nullptr;
}

QVariantList ConversationTransfer::turns(const QString &sessionId) const
{
    QVariantList result;
    const auto *session = find(sessionId);
    if (!session) return {};
    for (const int i : turnStarts(*session)) {
        const auto &m = session->messages[i];
        result.append(QString::number(result.size() + 1) + QStringLiteral(" · ")
                      + (m.content.isEmpty() ? m.title : m.content).simplified().left(48));
    }
    return result;
}

QVariantMap ConversationTransfer::preview(int mode, const QString &sourceId, const QString &targetId, int first, int last)
{
    if (m_copying) return {{"error", QStringLiteral("上一次会话附件仍在复制。")}};
    m_pending = false;
    const auto *source = find(sourceId);
    m_create = mode == Fork || mode == Extract;
    const auto *target = m_create ? nullptr : find(targetId);
    if (!source || mode < Send || mode > Merge || (!m_create && (!target || target == source || target->projectId != source->projectId)))
        return {{"error", QStringLiteral("请选择同一项目中的另一条会话。")}};
    const auto starts = turnStarts(*source);
    if (mode == Fork) first = 0;
    if (first < 0 || first > last || last >= starts.size()) return {{"error", QStringLiteral("请选择有效的回合范围。")}};
    m_source = *source;
    m_target = target ? *target : ChatSession{};
    m_result = m_target;
    if (m_create) {
        m_result.id = newId();
        m_result.projectId = source->projectId;
        m_result.workspaceId = source->workspaceId;
        m_result.title = source->title + (mode == Fork ? QStringLiteral(" · 分支") : QStringLiteral(" · 摘出"));
    }
    QSet<QByteArray> existing;
    for (const auto &msg : m_result.messages) existing.insert(sourceKey(m_result.id, msg));
    const int end = last + 1 < starts.size() ? starts[last + 1] : source->messages.size();
    int added = 0, skipped = 0;
    bool needsBoundary = true;
    for (int i = starts[first]; i < end; ++i) {
        auto msg = source->messages[i];
        if (startsTurn(msg)) needsBoundary = true;
        const auto key = sourceKey(sourceId, msg);
        if (existing.contains(key)) { ++skipped; continue; }
        existing.insert(key);
        // 源回合的开头可能已被去重；首个实际追加的条目仍须与目标末轮分开。
        msg.startsTurn = needsBoundary;
        needsBoundary = false;
        if (msg.originSession.isEmpty()) { msg.originSession = sourceId; msg.originMessage = msg.id; }
        msg.id = newId();
        // 只传可见快照；工具的活跃任务、窗口和块控制权仍归原会话。
        const bool agentOutput = msg.activity.value("serviceId") == "codex-agent";
        if (agentOutput) {
            // 整理历史只复制输出，目标会话不能复用原线程或回合的控制身份。
            for (const auto *field : {"runId","turnId","threadId","boundAccount","account","identity","cwd"}) msg.activity.remove(field);
        }
        msg.activity.remove("taskId");
        msg.activity.remove("serviceId");
        msg.activity.remove("blockId");
        msg.activity.remove("taskState");
        if ((agentOutput || msg.role == MessageRole::Tool) && msg.status == MessageStatus::Running) {
            // 复制已完成的是这一刻的输出快照，原任务仍可继续运行。
            msg.status = MessageStatus::Complete;
            msg.activity.insert("summary", msg.title + QStringLiteral(" · 运行时快照"));
        }
        if (msg.activity.contains("contentMode")) msg.activity.insert("contentMode", 1);
        m_result.messages.append(msg);
        ++added;
    }
    if (added > 0) {
        m_result.transfers.append(QVariantMap{{"operationId", newId()}, {"kind", mode}, {"sourceId", source->id},
            {"sourceTitle", source->title}, {"firstMessageId", source->messages[starts[first]].id},
            {"lastMessageId", source->messages[end - 1].id}, {"added", added}});
    }
    m_pending = added > 0;
    return {{"added", added}, {"duplicates", skipped}, {"target", m_result.title}, {"ready", m_pending}};
}

bool ConversationTransfer::commit()
{
    if (!m_pending) return false;
    m_pending = false;
    const auto start = m_target.messages.size();
    const auto &messages = std::as_const(m_result.messages);
    const bool hasImages = std::any_of(messages.cbegin() + start, messages.cend(),
                                       [](const ChatMessage &m) { return !m.attachments.isEmpty(); });
    if (!hasImages) return publish();

    // 仅复制本批新增的图片，重定向至目标会话自己的目录。失败时不发布半个回合。
    using Copies = std::pair<QList<ChatMessage>, QString>;
    m_copying = true;
    auto *watcher = new QFutureWatcher<Copies>(this);
    connect(watcher, &QFutureWatcher<Copies>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_copying = false;
        if (!result.second.isEmpty()) { emit failure(result.second); return; }
        m_result.messages = std::move(result.first);
        publish();
    });
    auto directory = m_repository.sessionDirectory(m_result.projectId, m_result.id);
    watcher->setFuture(QtConcurrent::run([copied = m_result.messages, start, directory]() mutable -> Copies {
        for (qsizetype i = start; i < copied.size(); ++i) {
            for (auto &value : copied[i].attachments) {
                const auto old = value.toMap();
                const auto source = QUrl(old.value("url").toString());
                if (!source.isLocalFile()) return {{}, QStringLiteral("选中的附件缺少本地原件。")};
                const auto image = importConversationImage(source.toLocalFile(), directory, old.value("name").toString());
                if (!image.error.isEmpty()) return {{}, image.error};
                value = image.attachment;
            }
        }
        return {std::move(copied), {}};
    }));
    return true;
}

bool ConversationTransfer::publish()
{
    const auto *source = find(m_source.id);
    const auto *target = m_create ? nullptr : find(m_target.id);
    if (!source || *source != m_source || (!m_create && (!target || *target != m_target))) {
        emit failure(QStringLiteral("会话已更新，请重新预览后提交。"));
        return false;
    }
    auto next = m_sessions;
    m_result.updatedAt = QDateTime::currentDateTimeUtc();
    if (m_create) next.prepend(m_result);
    else for (auto &session : next) if (session.id == m_result.id) { session = m_result; break; }
    auto catalog = m_catalog;
    const bool follow = catalog.selectedResource.isEmpty() && catalog.selectedSession == m_source.id;
    if (follow) catalog.selectedSession = m_result.id;
    QString error;
    if (!m_repository.save(catalog, next, &error)) { emit failure(error); return false; }
    m_sessions = std::move(next);
    m_catalog = std::move(catalog);
    const auto selected = m_catalog.selectedSession;
    m_source = {};
    m_target = {};
    m_result = {};
    emit published(selected);
    return true;
}
}
