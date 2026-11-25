#pragma once
#include <QByteArray>
#include <QStringList>

namespace exagent::computer {
QString driverPath();
int invokeDriver(const QString &program, const QStringList &arguments, const QByteArray &input = {}, bool schemaJson = false);
int fail(const QString &code, const QString &message);
}
