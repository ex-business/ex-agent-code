#pragma once
#include <QByteArray>
#include <QString>
namespace exagent::openai {
QByteArray credential(const QString &file, QString *error);
}
