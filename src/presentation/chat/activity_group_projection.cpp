#include "activity_group_projection.h"
#include "domain/content/content_block.h"
namespace exagent {
namespace {
bool groupable(const ChatMessage &m) {
    // 文件修改卡与动态媒体有自己的呈现，不能被普通工具摘要折叠吞掉。
    const int kind = m.activity.value("blockKind").toInt();
    return m.role == MessageRole::Tool && m.title != QStringLiteral("文件修改")
        && kind != int(content::BlockKind::Chart) && kind != int(content::BlockKind::Image)
        && kind != int(content::BlockKind::ImageSequence);
}
}
void ActivityGroupProjection::clear() { m_groups.clear(); m_membership.clear(); m_previousIds.clear(); m_expansion.clear(); }
void ActivityGroupProjection::rebuild(const QList<ChatMessage> &messages, int firstLoaded) {
    m_groups.clear(); m_membership.fill(-1, messages.size() - firstLoaded);
    QHash<QString, QString> nextIds;
    QHash<QString, int> nextExpansion;
    for (int i = firstLoaded; i < messages.size();) {
        if (!groupable(messages[i])) { ++i; continue; }
        int end = i + 1;
        const auto task = messages[i].activity.value("taskId").toString();
        while (end < messages.size() && groupable(messages[end]) && messages[end].activity.value("taskId").toString() == task) ++end;
        if (end - i < 2) { i = end; continue; }
        Group g; g.first = i - firstLoaded; g.last = end - firstLoaded - 1;
        // 页前插入和后续追加优先沿用已有成员的组ID，折叠状态不跟delegate生命周期走。
        for (int j = i; j < end && g.id.isEmpty(); ++j) g.id = m_previousIds.value(messages[j].id);
        if (g.id.isEmpty()) g.id = messages[i].id;
        int integrations = 0, commands = 0, images = 0, searches = 0;
        for (int j = i; j < end; ++j) {
            const auto &m = messages[j];
            const int kind = m.activity.value("kind").toInt();
            if (kind == Integration) ++integrations; else if (kind == Command) ++commands; else if (kind == Image) ++images; else if (kind == Search) ++searches;
            if (m.status == MessageStatus::Running) g.status = MessageStatus::Running;
            else if (g.status != MessageStatus::Running && m.status == MessageStatus::Failed) g.status = MessageStatus::Failed;
            else if (g.status == MessageStatus::Complete && m.status == MessageStatus::Cancelled) g.status = MessageStatus::Cancelled;
            nextIds.insert(m.id, g.id);
        }
        QStringList kinds;
        if (integrations) kinds << QStringLiteral("工具 %1").arg(integrations);
        if (commands) kinds << QStringLiteral("命令 %1").arg(commands);
        if (images) kinds << QStringLiteral("图片 %1").arg(images);
        if (searches) kinds << QStringLiteral("搜索 %1").arg(searches);
        const auto label = g.status == MessageStatus::Running ? QStringLiteral("正在执行") : g.status == MessageStatus::Failed ? QStringLiteral("部分操作失败") : g.status == MessageStatus::Cancelled ? QStringLiteral("操作已取消") : QStringLiteral("已完成");
        g.summary = label + QStringLiteral(" %1 项操作").arg(end - i);
        if (!kinds.isEmpty()) g.summary += QStringLiteral(" · ") + kinds.join(QStringLiteral(" · "));
        const int groupIndex = m_groups.size();
        for (int row = g.first; row <= g.last; ++row) m_membership[row] = groupIndex;
        nextExpansion.insert(g.id, m_expansion.value(g.id, Expanded));
        m_groups.append(g); i = end;
    }
    m_previousIds = std::move(nextIds); m_expansion = std::move(nextExpansion);
}
QVariantMap ActivityGroupProjection::info(int row) const {
    if (row < 0 || row >= m_membership.size() || m_membership[row] < 0) return {};
    const auto &g = m_groups[m_membership[row]];
    return {{"id", g.id}, {"first", g.first}, {"last", g.last}, {"count", g.last - g.first + 1}, {"summary", g.summary}, {"status", int(g.status)}, {"expansion", m_expansion.value(g.id, Expanded)}};
}
bool ActivityGroupProjection::head(int row) const { const auto v = info(row); return !v.isEmpty() && v.value("first").toInt() == row; }
bool ActivityGroupProjection::collapsed(int row) const { return info(row).value("expansion").toInt() == Collapsed; }
bool ActivityGroupProjection::hidden(int row) const { return collapsed(row) && !head(row); }
bool ActivityGroupProjection::toggle(int row) {
    const auto v = info(row); if (v.isEmpty()) return false;
    const auto id = v.value("id").toString();
    m_expansion[id] = m_expansion.value(id, Expanded) == Expanded ? Collapsed : Expanded;
    return true;
}
}
