#pragma once
#include <QJsonObject>
#include <QString>
namespace exagent::supervision {
QJsonObject readEnvelope(const QString &file, QString *error);
bool writeEnvelope(const QString &root, const QString &name, const QJsonObject &data, QString *error);
}
