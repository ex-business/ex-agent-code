#pragma once
#include "domain/chat/chat_message.h"
#include <QHash>
#include <QVariantMap>
#include <QVector>

namespace exagent {
// 分组仅是呈现投影：原始消息顺序、ID和分页行号保持不变。
class ActivityGroupProjection {
public:
    enum ActivityKind { Integration = 1, Command = 2, Image = 3, Search = 4 };
    enum Expansion { Expanded = 1, Collapsed = 2 };
    void clear();
    void rebuild(const QList<ChatMessage> &messages, int firstLoaded);
    QVariantMap info(int row) const;
    bool hidden(int row) const;
    bool head(int row) const;
    bool collapsed(int row) const;
    bool toggle(int row);
private:
    struct Group { QString id, summary; int first = 0, last = 0; MessageStatus status = MessageStatus::Complete; };
    QVector<Group> m_groups;
    QVector<int> m_membership;
    QHash<QString, QString> m_previousIds;
    QHash<QString, int> m_expansion;
};
}
