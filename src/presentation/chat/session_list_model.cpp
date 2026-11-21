#include "session_list_model.h"

namespace exagent {

SessionListModel::SessionListModel(QObject *parent) : QAbstractListModel(parent) {}
int SessionListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_items.size(); }
QVariant SessionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
    const auto &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case TitleRole: return item.title;
    case PreviewRole: return item.preview;
    case DateRole: return item.date;
    default: return {};
    }
}
QHash<int, QByteArray> SessionListModel::roleNames() const
{
    return {{IdRole, "sessionId"}, {TitleRole, "sessionTitle"}, {PreviewRole, "preview"}, {DateRole, "dateLabel"}};
}
void SessionListModel::replace(const QList<ChatSession> &sessions)
{
    beginResetModel();
    m_items.clear();
    for (const auto &session : sessions) {
        m_items.append({session.id, session.title,
                        session.messages.isEmpty() ? QString() : session.messages.last().content.left(100),
                        session.updatedAt.toLocalTime().toString("MM/dd")});
    }
    endResetModel();
}

} // namespace exagent
