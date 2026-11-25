#pragma once
#include <QJsonObject>

namespace exagent::computer {
QString validateWindowAction(const QString &command, const QJsonObject &input);
}
