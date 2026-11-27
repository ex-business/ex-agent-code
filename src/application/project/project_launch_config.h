#pragma once
#include <QVariantMap>

namespace exagent {
// EX 扩展字段放在独立命名空间，其他 Orca 配置交由各自的插件处理。
QVariantMap readLaunchDefaults(const QString &root, QString *error);
QString writeLaunchTemplate(const QString &root);
}
