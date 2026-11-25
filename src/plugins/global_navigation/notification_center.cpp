#include "notification_center.h"
#include "domain/content/content_block.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QGuiApplication>
#include <algorithm>
#ifdef Q_OS_MACOS
#include "platform/macos/activity_notifications.h"
#endif

namespace exagent {
namespace {
QString fingerprint(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}
NotificationCenter::NotificationCenter(QObject *parent) : QObject(parent)
{
    QSettings prefs;
    m_entries = prefs.value("activityInbox/entries").toList().mid(0,100);
    m_attention = prefs.value("activityInbox/attention",true).toBool();
    m_outcomes = prefs.value("activityInbox/outcomes",true).toBool();
    m_desktop = prefs.value("activityInbox/desktop",false).toBool();
    m_badge = prefs.value("activityInbox/badge",true).toBool();
    m_sound = prefs.value("activityInbox/sound",false).toBool();
#ifdef Q_OS_MACOS
    if (nativeAvailable()) installNoticeDelegate(this);
#endif
}
int NotificationCenter::unread() const
{
    return std::count_if(m_entries.cbegin(),m_entries.cend(),[](const auto &v){ return !v.toMap()["read"].toBool(); });
}
bool NotificationCenter::nativeAvailable() const
{
#ifdef Q_OS_MACOS
    return nativeNoticeAvailable();
#else
    return false;
#endif
}
void NotificationCenter::setAttention(bool v) { m_attention=v; QSettings().setValue("activityInbox/attention",v); emit settingsChanged(); }
void NotificationCenter::setOutcomes(bool v) { m_outcomes=v; QSettings().setValue("activityInbox/outcomes",v); emit settingsChanged(); }
void NotificationCenter::setSound(bool v) { m_sound=v; QSettings().setValue("activityInbox/sound",v); emit settingsChanged(); }
void NotificationCenter::updateBadge()
{
    // 只由会话存储的写入实例维护程序角标，防止只读窗口覆盖未读数。
    if (m_managesBadge) qGuiApp->setBadgeNumber(m_badge ? unread() : 0);
}
void NotificationCenter::setManagesBadge(bool v) { m_managesBadge=v; updateBadge(); }
void NotificationCenter::setBadge(bool v)
{
    m_badge=v; QSettings().setValue("activityInbox/badge",v); updateBadge(); emit settingsChanged();
}
void NotificationCenter::setDesktop(bool v)
{
    m_desktop=v; QSettings().setValue("activityInbox/desktop",v); setDeliveryError({}); emit settingsChanged();
#ifdef Q_OS_MACOS
    if (v && nativeAvailable()) requestNativeNotices(this);
#endif
}
void NotificationCenter::setDeliveryError(const QString &error)
{
    if (m_error == error) return;
    m_error=error; emit deliveryErrorChanged();
}
void NotificationCenter::save()
{
    QSettings().setValue("activityInbox/entries",m_entries);
    updateBadge();
    emit changed();
}
void NotificationCenter::mark(const QString &id, bool read)
{
    for (auto &value : m_entries) {
        auto row=value.toMap();
        if (row["id"] != id || row["read"].toBool() == read) continue;
        row["read"]=read; value=row; save(); return;
    }
}
void NotificationCenter::append(int category, QString key, QVariantMap row)
{
    if (!(category == Attention ? m_attention : m_outcomes)) return;
    const auto id=fingerprint(key);
    for (const auto &old : std::as_const(m_entries)) if (old.toMap()["id"] == id) return;
    row.insert("id",id); row.insert("category",category); row.insert("read",false);
    row.insert("time",QDateTime::currentMSecsSinceEpoch());
    m_entries.prepend(row);
    if (m_entries.size()>100) m_entries.removeLast();
    save();
#ifdef Q_OS_MACOS
    if (m_desktop && nativeAvailable()) deliverNativeNotice(this,id,m_sound);
#endif
}
void NotificationCenter::record(int category, const QString &session, const QString &run, const QString &token, const QString &title)
{
    if (session.isEmpty() || run.isEmpty() || category<Attention || category>Interrupted) return;
    // 只持久化请求指纹；通知没有授予工具权限的能力。
    const auto hash=token.isEmpty() ? QString{} : fingerprint(token);
    append(category,session+"/"+run+"/"+QString::number(category)+"/"+hash,
           {{"session",session},{"run",run},{"requestHash",hash},{"title",title}});
}
bool NotificationCenter::matchesRequest(const QVariantMap &entry,const QString &token) const
{
    return !token.isEmpty() && entry["requestHash"] == fingerprint(token);
}
void NotificationCenter::observeTerminals(const QVariantList &rows)
{
    using State=content::TaskState;
    QHash<QString,int> next;
    for (const auto &value : rows) {
        const auto row=value.toMap(); const auto id=row["terminal"].toString();
        if (id.isEmpty()) continue;
        const auto state=State(row["state"].toInt());
        const int kind=row["pending"].toInt() ? Attention : state==State::Complete ? Completed :
            state==State::Failed ? Failed : state==State::Cancelled ? Stopped : 0;
        next.insert(id,kind);
        // 初次发现已完成的快照只用于导航。仍需人工处理的提示可以即时提醒。
        if (kind && kind!=m_terminalStates.value(id) && (kind==Attention || m_terminalStates.contains(id)))
            append(kind,id+"/"+QString::number(kind)+"/"+row["time"].toString(),{{"terminal",id},{"title",row["title"]}});
    }
    m_terminalStates=next;
}
}
