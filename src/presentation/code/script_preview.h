#pragma once
#include <QString>

namespace exagent {
// 展示层只识别单个字面量脚本，含动态 shell 展开或不明确语法时回退原命令。
QString pythonCommandPreview(const QString &command);
}
