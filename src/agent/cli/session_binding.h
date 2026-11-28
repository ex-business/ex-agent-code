#pragma once
#include <QJsonObject>
#include <QString>
namespace exagent::cli {
enum Provider { Pi = 2, Claude = 3 };
QString leaseKey(const QJsonObject &source);
bool validSource(const QJsonObject &source, QString *error);
QString executable(int provider);
QString installPiGuard();
int terminalContinuation(const QJsonObject &binding);
} // namespace exagent::cli
