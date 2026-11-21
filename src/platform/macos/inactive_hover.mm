#include "inactive_hover.h"
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QPointer>
#include <QQuickWindow>
#import <AppKit/AppKit.h>

namespace {
NSCursor *nativeCursor(Qt::CursorShape shape)
{
    switch (shape) {
    case Qt::IBeamCursor: return NSCursor.IBeamCursor;
    case Qt::PointingHandCursor: return NSCursor.pointingHandCursor;
    case Qt::SplitHCursor:
    case Qt::SizeHorCursor: return NSCursor.resizeLeftRightCursor;
    case Qt::SplitVCursor:
    case Qt::SizeVerCursor: return NSCursor.resizeUpDownCursor;
    default: return NSCursor.arrowCursor;
    }
}
}

@interface EXInactiveHover : NSObject {
    QPointer<QQuickWindow> _window;
    NSView *_view;
    NSTrackingArea *_area;
    QPointF _previous;
    bool _inside;
    bool _cursorPending;
}
- (instancetype)initWithView:(NSView *)view window:(QQuickWindow *)window;
- (void)updateHover:(NSEvent *)event;
- (void)applyCursor;
@end

@implementation EXInactiveHover
- (instancetype)initWithView:(NSView *)view window:(QQuickWindow *)window
{
    if ((self = [super init])) {
        _window = window;
        _view = [view retain];
        // Qt 的 Cocoa 路径跳过未激活应用的悬停；独立 tracking area 只补这条路径。
        // ActiveAlways 不支持 CursorUpdate，因此由真实 mouseMoved/Entered 更新系统光标。
        _area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
            options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
            owner:self userInfo:nil];
        [_view addTrackingArea:_area];
    }
    return self;
}
- (void)dealloc
{
    [_view removeTrackingArea:_area];
    [_area release];
    [_view release];
    [super dealloc];
}
- (void)updateHover:(NSEvent *)event
{
    if (!_window || NSApp.active || !_window->isVisible() || NSEvent.pressedMouseButtons != 0) return;
    NSWindow *native = _view.window;
    // 被别的窗口遮挡时不能修改其他应用的光标；此查询按真实鼠标命中排除透明覆盖层。
    if (!native || [NSWindow windowNumberAtPoint:NSEvent.mouseLocation belowWindowWithWindowNumber:0] != native.windowNumber) return;
    const NSPoint point = [_view convertPoint:event.locationInWindow fromView:nil];
    const QPointF position(point.x, _view.isFlipped ? point.y : _view.bounds.size.height - point.y);
    QHoverEvent hover(QEvent::HoverMove, position, QCursor::pos(), _inside ? _previous : position,
                      QGuiApplication::queryKeyboardModifiers());
    // 复用 Quick 的命中、裁剪、链接和按钮光标规则，不维护另一套文字区域索引。
    QCoreApplication::sendEvent(_window.data(), &hover);
    if (!_window) return;
    _previous = position;
    _inside = true;
    // AppKit 可能在 tracking 回调后恢复原窗口光标；等当前原生事件处理完再设置。
    // 同一轮移动只排一次，block 持有 tracker，窗口关闭后由 QPointer 阻止访问。
    if (!_cursorPending) {
        _cursorPending = true;
        [[NSOperationQueue mainQueue] addOperationWithBlock:^{
            self->_cursorPending = false;
            [self applyCursor];
        }];
    }
}
- (void)applyCursor
{
    if (!_window || NSApp.active || !_inside || NSEvent.pressedMouseButtons != 0) return;
    NSWindow *native = _view.window;
    if (!native || [NSWindow windowNumberAtPoint:NSEvent.mouseLocation belowWindowWithWindowNumber:0] != native.windowNumber) return;
    [nativeCursor(_window->cursor().shape()) set];
}
- (void)mouseMoved:(NSEvent *)event { [self updateHover:event]; }
- (void)mouseEntered:(NSEvent *)event { [self updateHover:event]; }
- (void)mouseExited:(NSEvent *)event
{
    Q_UNUSED(event);
    if (_window && _inside && !NSApp.active) {
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(_window.data(), &leave);
    }
    _inside = false;
    // 离开后由新目标窗口管理系统光标，不能在这里强制设置箭头。
}
@end

void installMacInactiveHover(QQuickWindow *window)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view) return;
    EXInactiveHover *tracker = [[EXInactiveHover alloc] initWithView:view window:window];
    QObject::connect(window, &QObject::destroyed, [tracker] { [tracker release]; });
}

int macSystemCursorShape()
{
    NSCursor *system = NSCursor.currentSystemCursor;
    NSData *image = system.image.TIFFRepresentation;
    if (!image) return -1;
    for (const auto shape : {Qt::ArrowCursor, Qt::IBeamCursor, Qt::PointingHandCursor, Qt::SplitHCursor, Qt::SplitVCursor}) {
        NSCursor *candidate = nativeCursor(shape);
        if ([image isEqualToData:candidate.image.TIFFRepresentation]) return int(shape);
    }
    return -1;
}
