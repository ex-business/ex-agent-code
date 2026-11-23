#pragma once
#include <QString>

namespace exagent {
// 使用与桌面宿主相同的写锁和索引发布流程，将旧会话资源一次性转入项目目录。
bool migrateHistory(const QString &directory, QString *error);
}
