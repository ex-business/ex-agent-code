#pragma once
#include <QVariantMap>

namespace exagent {
// 未跟踪文件取自当前工作区，不存在可由 Git 索引寻址的对象。
QVariantMap previewUntracked(const QString &folder, const QString &path);
}
