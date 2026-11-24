#pragma once
#include <QVariantMap>
namespace exagent::activity {
enum Status { Unknown, Working, Waiting, Done, Failed, Stopped };
QString directory(const QString& cwd);
void publish(const QString& cwd, const QString& run, const QVariantMap& snapshot);
QVariantList recent(const QString& cwd);
QVariantMap memory(const QString& cwd, const QString& relative);
}
