#pragma once
#include "domain/chat/chat_message.h"
namespace exagent {
// 只导出用户选定的连续回合；附件须能解析到本会话自己的资源目录。
QVariantMap historyContext(const ChatSession &session, const QString &directory, int first, int last);
}
