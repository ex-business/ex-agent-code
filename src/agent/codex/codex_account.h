#pragma once
#include <QProcess>
#include <QJsonObject>

namespace exagent::codex {
enum Account { System = 1, Dedicated = 2 };
QString executable();
QString identity(Account profile, const QJsonObject &account);
void noteDedicatedLogin();
bool prepare(QProcess &process, Account account, QString *error, const QString &home = {});
QStringList accountArguments(Account account);
}
