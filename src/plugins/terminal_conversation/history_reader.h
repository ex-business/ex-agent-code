#pragma once
#include "domain/chat/chat_message.h"
#include <QVariantMap>
#include <functional>
namespace exagent::terminalchat {
// 第三方历史在此边界转换；界面继续使用现有消息与富文本组件。
struct Snapshot {
  QList<ChatMessage> messages;
  QString error;
};
enum Provider { Codex = 1, Pi = 2, ClaudeCode = 3 };
using Reader = std::function<Snapshot(const QVariantMap &)>;
// 内置适配器不可覆盖；扩展以独立数字ID注册自己的只读转换器。
bool registerReader(int provider, Reader reader);
Snapshot read(const QVariantMap &binding);
Snapshot readJsonl(const QVariantMap &binding);
Snapshot readCodex(const QVariantMap &binding);
} // namespace exagent::terminalchat
