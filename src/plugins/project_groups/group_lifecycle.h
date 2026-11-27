#pragma once
#include "application/project/project_navigator.h"
#include <QJsonObject>
namespace exagent::groups {
// 生命周期仅变更目录归属；会话与工作区身份保持稳定，不搬移资源或触碰Git目录。
QJsonObject lifecycle(ProjectNavigator &navigator, const QString &action,
                      const QJsonObject &params, ProjectCatalog *next,
                      QString *error);
} // namespace exagent::groups
