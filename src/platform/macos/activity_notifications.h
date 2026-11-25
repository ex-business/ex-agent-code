#pragma once
#include <QString>
namespace exagent {
class NotificationCenter;
bool nativeNoticeAvailable();
void installNoticeDelegate(NotificationCenter *owner);
void requestNativeNotices(NotificationCenter *owner);
void deliverNativeNotice(NotificationCenter *owner,const QString &id,bool sound);
}
