#include "activity_notifications.h"
#include "plugins/global_navigation/notification_center.h"
#include <QPointer>
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

@interface EXActivityDelegate : NSObject <UNUserNotificationCenterDelegate>
@end
namespace {
QPointer<exagent::NotificationCenter> receiver;
}
@implementation EXActivityDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion
{
    completion(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}
- (void)userNotificationCenter:(UNUserNotificationCenter *)center didReceiveNotificationResponse:(UNNotificationResponse *)response
         withCompletionHandler:(void (^)(void))completion
{
    const QString id = QString::fromNSString(response.notification.request.identifier);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (receiver) emit receiver->activated(id);
    });
    completion();
}
@end
namespace exagent {
bool nativeNoticeAvailable()
{
    // 命令行或协议 worker 没有应用包身份，不能向 macOS 注册通知。
    return [NSBundle.mainBundle.bundleURL.pathExtension isEqualToString:@"app"] && NSBundle.mainBundle.bundleIdentifier.length;
}
void installNoticeDelegate(NotificationCenter *owner)
{
    static EXActivityDelegate *delegate = [[EXActivityDelegate alloc] init];
    receiver=owner;
    UNUserNotificationCenter.currentNotificationCenter.delegate=delegate;
}
void requestNativeNotices(NotificationCenter *owner)
{
    installNoticeDelegate(owner);
    [UNUserNotificationCenter.currentNotificationCenter requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
        completionHandler:^(BOOL granted, NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (receiver == owner && receiver->desktop())
                    receiver->setDeliveryError(granted && !error ? QString{} : QStringLiteral("请在系统设置中允许通知"));
            });
        }];
}
void deliverNativeNotice(NotificationCenter *owner,const QString &id,bool sound)
{
    // macOS 可能接收未授权的请求但不展示，不能只凭提交回调判断权限。
    // 请求标识在调用栈内转换，异步回调由 ARC 持有副本。
    NSString *identifier = [id.toNSString() copy];
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (receiver != owner || !receiver->desktop()) return;
            const auto status = settings.authorizationStatus;
            if (status != UNAuthorizationStatusAuthorized && status != UNAuthorizationStatusProvisional) {
                receiver->setDeliveryError(QStringLiteral("请在系统设置中允许通知"));
                return;
            }
            receiver->setDeliveryError({});
            UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
            content.title = @"EX Agent Code";
            content.body = receiver->desktopBody().toNSString();
            if (sound && receiver->sound()) content.sound = UNNotificationSound.defaultSound;
            // 系统锁屏只显示通用提示；会话、路径、命令和审批令牌都留在应用内。
            UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:identifier
                content:content trigger:nil];
            [center addNotificationRequest:request withCompletionHandler:^(NSError *error) {
                if (error) dispatch_async(dispatch_get_main_queue(), ^{
                    if (receiver == owner && receiver->desktop())
                        receiver->setDeliveryError(QStringLiteral("系统通知未送达，请检查通知权限"));
                });
            }];
        });
    }];
}
}
