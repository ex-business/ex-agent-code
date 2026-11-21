#include "message_list_model.h"

#include <utility>
#include <QTextDocument>
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
    case BlocksRole: {
        if (message.role != MessageRole::Assistant) return QVariantList{};
        if (const auto *cached = m_blocksCache.object(message.id)) return *cached;
        auto parsed = blocks(message.content);
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
            {BlocksRole, "contentBlocks"}, {TimeRole, "timeLabel"}};
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

QVariantList MessageListModel::blocks(const QString &content)
{
    QVariantList result;
    qsizetype blockStart = 0;
    qsizetype offset = 0;
    QString language;
    bool inCode = false;
    const auto appendBlock = [&](qsizetype end) {
        if (end <= blockStart) return;
        auto text = content.mid(blockStart, end - blockStart);
        if (text.endsWith('\n')) text.chop(1);
        QString html;
        if (!inCode) {
            text = text.trimmed();
            if (text.isEmpty()) return;
            QTextDocument document;
            document.setMarkdown(text);
            html = document.toHtml();
            // 统一表格留白与浅边框，避免历史 Markdown 使用默认的黑色网格。
            html.replace(QRegularExpression("<table[^>]*>"), "<table border=\"1\" cellspacing=\"0\" cellpadding=\"8\" style=\"border-collapse:collapse; border-color:#d9dde3;\">");
            html.replace(QRegularExpression("<body[^>]*>"), "<body>");
            html.replace(QRegularExpression("margin-top:[^;]+;"), "margin-top:0px;");
            html.replace(QRegularExpression("margin-bottom:[^;]+;"), "margin-bottom:5px;");
            html.replace(QRegularExpression("(<(?:p|li)[^>]*style=\")"), "\\1line-height:145%; ");
        }
        result.append(QVariantMap{{"kind", inCode ? 1 : 0}, {"text", text},
                                  {"html", html}, {"language", language}});
    };
    while (offset < content.size()) {
        auto end = content.indexOf('\n', offset);
        if (end < 0) end = content.size();
        const auto line = QStringView(content).mid(offset, end - offset);
        if (line.startsWith(u"```")) {
            appendBlock(offset);
            if (!inCode) language = line.mid(3).toString().trimmed();
            inCode = !inCode;
            blockStart = end + 1;
        }
        offset = end + 1;
    }
    appendBlock(content.size());
    return result;
}

} // namespace exagent
