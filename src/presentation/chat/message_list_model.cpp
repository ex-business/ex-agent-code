#include "message_list_model.h"

#include <utility>
#include "presentation/text/markdown_formatter.h"
#include <QRegularExpression>

namespace exagent {

MessageListModel::MessageListModel(QObject *parent) : QAbstractListModel(parent) {}
int MessageListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_messages.size() - m_firstLoaded; }
int MessageListModel::count() const { return rowCount(); }
int MessageListModel::totalCount() const { return m_pagedTotal >= 0 ? m_pagedTotal : m_messages.size(); }
bool MessageListModel::hasOlder() const { return m_pagedTotal >= 0 ? count() < m_pagedTotal : m_firstLoaded > 0; }

int MessageListModel::loadOlder()
{
    if (m_pagedTotal >= 0) {
        if (!hasOlder() || !m_olderLoader) return 0;
        auto older = m_olderLoader();
        const int added = older.size();
        if (added == 0) return 0;
        beginInsertRows({}, 0, added - 1);
        older.append(m_messages);
        m_messages = std::move(older);
        endInsertRows();
        emit countChanged();
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
        auto parsed = MarkdownFormatter().format(message.content, m_dark);
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

void MessageListModel::replace(QList<ChatMessage> messages)
{
    beginResetModel();
    m_messages = std::move(messages);
    m_pagedTotal = -1;
    m_olderLoader = {};
    m_firstLoaded = qMax(0, static_cast<int>(m_messages.size()) - 30);
    m_blocksCache.clear();
    endResetModel();
    emit countChanged();
}

void MessageListModel::replacePaged(QList<ChatMessage> latest, int total, std::function<QList<ChatMessage>()> olderLoader)
{
    // 资源模式只保存已读取的页；未翻阅的历史不读取、不转换 Markdown。
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
