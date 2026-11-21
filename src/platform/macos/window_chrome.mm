#include "window_chrome.h"
#include <QQuickWindow>
#import <AppKit/AppKit.h>

void configureMacWindowChrome(QQuickWindow *window)
{
    // 只适配窗口装饰；原生红黄绿按钮、缩放、全屏和系统窗口管理保持可用。
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *native = view.window;
    if (!native) return;
    native.titleVisibility = NSWindowTitleHidden;
    native.titlebarAppearsTransparent = YES;
}
