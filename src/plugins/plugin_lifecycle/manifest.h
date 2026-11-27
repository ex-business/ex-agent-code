#pragma once
#include <QJsonObject>
namespace exagent::plugins {
// 自有执行级别和生命周期使用数字契约，能力标识保持稳定字符串。
enum Execution { Data = 1, Declarative = 2 };
enum State { PendingApproval = 0, Enabled = 1, Disabled = 2 };
QJsonObject validateManifest(const QJsonObject &input, QString *error);
QJsonObject readManifest(const QString &directory, QString *error);
} // namespace exagent::plugins
