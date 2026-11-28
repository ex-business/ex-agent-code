#pragma once
#include <QVariantMap>
namespace exagent {
// 读取tmux保留的逻辑行；有界快照不改变终端输入与copy-mode。
QVariantMap terminalScrollback(const QString &id, const QString &fallback);
} // namespace exagent
