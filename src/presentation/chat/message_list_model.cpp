#include "message_list_model.h"

#include <utility>
#include "presentation/text/markdown_formatter.h"
#include <QRegularExpression>
#include <QTextDocument>
#include <QFutureWatcher>
#include <QTimer>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(resourceBlockLog, "exagent.content.resource")

namespace exagent {

MessageListModel::MessageListModel(QObject *parent) : QAbstractListModel(parent), m_fallbackBlocks(this), m_contentStore(&m_fallbackBlocks) {}
void MessageListModel::setContentContext(const QString &conversationId, ContentBlockStore *store)
{
    m_conversationId = conversationId;
    m_contentStore = store ? store : &m_fallbackBlocks;
    m_blocksCache.clear();
}
int MessageListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_messages.size() - m_firstLoaded; }
int MessageListModel::count() const { return rowCount(); }
int MessageListModel::totalCount() const { return m_pagedTotal >= 0 ? m_pagedTotal : m_messages.size(); }
bool MessageListModel::hasOlder() const { return m_pagedTotal >= 0 ? count() < m_pagedTotal : m_firstLoaded > 0; }

int MessageListModel::loadOlder()
{
    if (m_pagedTotal >= 0) {
        if (!hasOlder() || !m_olderLoader) return 0;
        if (m_pageBuffer.isEmpty()) { m_pageFailed = false; prefetchOlder(); return 0; }
        auto older = std::move(m_pageBuffer); m_pageBuffer.clear(); --m_nextPage;
        const int added = older.size();
        if (added == 0) return 0;
        beginInsertRows({}, 0, added - 1);
        older.append(m_messages);
        m_messages = std::move(older);
        endInsertRows();
        emit countChanged(); emit paginationChanged();
        return added;
    }
    const int added = qMin(30, m_firstLoaded);
    if (added == 0) return 0;
    // 旧消息按页插入，初次打开只向视图暴露最近 30 条。
    beginInsertRows({}, 0, added - 1);
    m_firstLoaded -= added;
    endInsertRows();
    emit countChanged();
    return added;
}
void MessageListModel::prefetchOlder()
{
    if (m_pagedTotal < 0 || !hasOlder() || !m_olderLoader || m_nextPage < 0 || m_pageFailed || m_loadingPage || !m_pageBuffer.isEmpty() || m_activeReaders >= 2) return;
    static QThreadPool readers;
    readers.setMaxThreadCount(2); readers.setThreadPriority(QThread::LowPriority);
    m_loadingPage = true; ++m_activeReaders; emit paginationChanged();
    const auto generation = m_pageGeneration;
    const auto cancel = m_pageCancel = std::make_shared<std::atomic_bool>(false);
    auto *watcher = new QFutureWatcher<OlderPage>(this);
    connect(watcher, &QFutureWatcher<OlderPage>::finished, this, [this, watcher, generation, cancel] {
        --m_activeReaders;
        if (generation == m_pageGeneration && !cancel->load()) {
            m_loadingPage = false;
            try {
                auto page = watcher->result();
                if (!page.error.isEmpty() || page.messages.isEmpty()) { m_pageFailed = true; emit pageFailure(page.error.isEmpty() ? QStringLiteral("历史页为空，已保留当前内容") : page.error); }
                else { m_pageBuffer = std::move(page.messages); emit olderPageReady(); }
            } catch (...) { m_pageFailed = true; emit pageFailure(QStringLiteral("历史预取失败，已保留当前内容")); }
            emit paginationChanged();
        }
        watcher->deleteLater();
        if (generation != m_pageGeneration && hasOlder()) QTimer::singleShot(0, this, &MessageListModel::prefetchOlder);
    });
    // 文件读取仅捕获无GUI依赖的读取函数与页号；结果按会话代次校验后才交给模型。
    watcher->setFuture(QtConcurrent::run(&readers, [reader = m_olderLoader, page = m_nextPage, cancel] {
        if (cancel->load()) return OlderPage{};
        auto result = reader(page); return cancel->load() ? OlderPage{} : result;
    }));
}
const QList<ChatMessage> &MessageListModel::messages() const { return m_messages; }

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto &message = m_messages.at(m_firstLoaded + index.row());
    switch (role) {
    case IdRole: return message.id;
    case KindRole: return static_cast<int>(message.role);
    case ContentRole: return message.content;
    case TitleRole: return message.title;
    case StatusRole: return static_cast<int>(message.status);
    case ExpandedRole: return message.expanded;
    case ActivityRole: return message.activity;
    case AttachmentsRole: return message.attachments;
    case FilesRole: {
        if (!message.files.isEmpty()) return message.files;
        QVariantList files;
        if (message.role != MessageRole::Tool || message.title != QStringLiteral("文件修改")) return files;
        static const QRegularExpression path("^[^\\n]+\\.(?:qml|cpp|h|mm|md|json|go|js|ts|tsx|jsx|py|txt)$");
        // 兼容早期文本资源：只展示原记录列出的文件，不补造增删行数。
        for (const auto &line : message.content.split('\n')) if (path.match(line.trimmed()).hasMatch()) {
            files.append(QVariantMap{{"path", line.trimmed()}});
            if (files.size() >= 500) break;
        }
        return files;
    }
    case BlocksRole: {
        if (message.role != MessageRole::Assistant && message.role != MessageRole::Notice) return QVariantList{};
        if (const auto *cached = m_blocksCache.object(message.id)) return *cached;
        auto parsed = MarkdownFormatter().split(message.content);
        QString error;
        if (m_contentStore) {
            auto projected = ResourceBlockAdapter(*m_contentStore).project(m_conversationId, message.id, parsed, &error);
            if (error.isEmpty()) parsed = std::move(projected);
            // 超过统一快照预算时仍保留旧渲染原文，不让架构迁移静默丢失历史内容。
            else qCWarning(resourceBlockLog).noquote() << "资源块归约失败，保留原文展示，messageId：" << message.id << "原因：" << error;
        }
        qsizetype bytes = 0;
        for (const auto &entry : parsed) {
            const auto block = entry.toMap();
            bytes += (block.value("text").toString().size() + block.value("html").toString().size()) * 2;
        }
        m_blocksCache.insert(message.id, new QVariantList(parsed), qMax(1, static_cast<int>((bytes + 1023) / 1024)));
        return parsed;
    }
    case TimeRole: return message.createdAt.toLocalTime().toString("HH:mm");
    default: return {};
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {{IdRole, "messageId"}, {KindRole, "messageKind"}, {ContentRole, "content"},
            {TitleRole, "messageTitle"}, {StatusRole, "messageStatus"}, {ExpandedRole, "expanded"},
            {BlocksRole, "contentBlocks"}, {TimeRole, "timeLabel"}, {FilesRole, "changedFiles"},
            {ActivityRole, "activity"}, {AttachmentsRole, "attachments"}};
}

QStringList MessageListModel::selectionBlockKeys(int row) const
{
    if (row < 0 || row >= count()) return {};
    const auto &message = m_messages.at(m_firstLoaded + row);
    if (message.role == MessageRole::User) return {message.id + ".user"};
    if (message.role == MessageRole::Tool) {
        if (message.title == QStringLiteral("文件修改")) {
            QStringList keys;
            for (const auto &value : data(index(row), FilesRole).toList()) keys.append(message.id + ".file." + value.toMap().value("path").toString());
            return keys;
        }
        return {message.id + ".summary", message.activity.value("blockId", message.id + ".details").toString()};
    }
    QStringList keys;
    for (const auto &value : data(index(row), BlocksRole).toList()) {
        const auto block = value.toMap(); keys.append(block.value("blockId", message.id + ".block." + QString::number(keys.size())).toString());
    }
    return keys;
}

QStringList MessageListModel::selectionBlocks(int row) const
{
    if (row < 0 || row >= count()) return {};
    const auto &message = m_messages.at(m_firstLoaded + row);
    if (message.role == MessageRole::User) {
        auto text = message.content;
        text.remove(QRegularExpression("[\\r\\n]+$"));
        QTextDocument document;
        document.setHtml(MarkdownFormatter().userPresentation(text, QFont()).value("html").toString());
        return {document.toRawText()};
    }
    if (message.role == MessageRole::Tool) {
        if (message.title == QStringLiteral("文件修改")) {
            QStringList paths;
            for (const auto &entry : data(index(row), FilesRole).toList()) paths.append(entry.toMap().value("path").toString());
            return paths;
        }
        const auto kind = message.activity.value("kind").toInt();
        const auto summary = message.activity.value("summary").toString();
        QStringList blocks{!summary.isEmpty() ? summary : kind == 3 ? QStringLiteral("已查看 %1 张图像").arg(message.attachments.size())
            : message.title.startsWith(QStringLiteral("运行命令")) && message.status == MessageStatus::Complete ? QStringLiteral("已") + message.title : message.title};
        if (message.expanded && !message.content.isEmpty()) blocks.append(message.content);
        return blocks;
    }
    QStringList result;
    for (const auto &entry : MarkdownFormatter().format(message.content, m_dark)) {
        const auto block = entry.toMap();
        if (block.value("kind").toInt() == 1) result.append(block.value("text").toString());
        else {
            QTextDocument document;
            document.setHtml(block.value("html").toString());
            result.append(document.toRawText());
        }
    }
    return result;
}

void MessageListModel::replace(QList<ChatMessage> messages)
{
    ++m_pageGeneration; if (m_pageCancel) m_pageCancel->store(true);
    m_loadingPage = false; m_pageFailed = false; m_pageBuffer.clear();
    beginResetModel();
    m_messages = std::move(messages);
    m_pagedTotal = -1;
    m_olderLoader = {};
    m_firstLoaded = qMax(0, static_cast<int>(m_messages.size()) - 30);
    m_blocksCache.clear();
    endResetModel();
    emit countChanged();
}

void MessageListModel::replacePaged(QList<ChatMessage> latest, int total, int nextPage, std::function<OlderPage(int)> olderLoader)
{
    // 资源模式只保留已读取页和最多一个预取页，不在主线程读取历史文件。
    ++m_pageGeneration; if (m_pageCancel) m_pageCancel->store(true);
    m_loadingPage = false; m_pageFailed = false; m_pageBuffer.clear(); m_nextPage = nextPage;
    beginResetModel();
    m_messages = std::move(latest);
    m_firstLoaded = 0;
    m_pagedTotal = total;
    m_olderLoader = std::move(olderLoader);
    m_blocksCache.clear();
    endResetModel();
    emit countChanged();
}

void MessageListModel::append(ChatMessage message)
{
    const auto row = rowCount();
    beginInsertRows({}, row, row);
    m_messages.append(std::move(message));
    endInsertRows();
    emit countChanged();
    emit contentUpdated();
}

void MessageListModel::upsertServiceMessage(ChatMessage message)
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages[row].id != message.id) continue;
        // 后台更新不重置展开状态和整份列表，避免打断阅读、滚动和文字选择。
        message.expanded = m_messages[row].expanded;
        m_messages[row] = std::move(message);
        m_blocksCache.remove(m_messages[row].id);
        if (row >= m_firstLoaded) emit dataChanged(index(row - m_firstLoaded), index(row - m_firstLoaded),
            {ContentRole, TitleRole, StatusRole, ActivityRole, BlocksRole});
        emit contentUpdated(); return;
    }
    append(std::move(message));
}

void MessageListModel::appendText(const QString &messageId, const QString &delta)
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages[row].id != messageId) continue;
        // 流式内容仅通知当前消息，避免重新创建整段历史的 delegate。
        m_messages[row].content.append(delta);
        m_blocksCache.remove(messageId);
        const int visibleRow = row - m_firstLoaded;
        if (visibleRow >= 0) emit dataChanged(index(visibleRow), index(visibleRow), {ContentRole, BlocksRole});
        emit contentUpdated();
        return;
    }
}

void MessageListModel::toggleExpanded(int row)
{
    if (row < 0 || row >= rowCount()) return;
    auto &message = m_messages[m_firstLoaded + row];
    message.expanded = !message.expanded;
    emit dataChanged(index(row), index(row), {ExpandedRole});
    emit presentationChanged();
}

void MessageListModel::setDark(bool dark)
{
    if (m_dark == dark) return;
    m_dark = dark;
    m_blocksCache.clear();
    if (count() > 0) emit dataChanged(index(0), index(count() - 1), {BlocksRole});
}

} // namespace exagent
