#pragma once
class QQuickWindow;

// 仅补充未激活窗口的悬停，不激活窗口或派发点击。
void installMacInactiveHover(QQuickWindow *window);
// 诊断读取系统正在显示的光标；无法识别时返回 -1。
int macSystemCursorShape();
