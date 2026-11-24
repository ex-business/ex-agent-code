#pragma once
#include <QStringList>
#include <QVariantMap>
namespace exagent {
struct TerminalResult {
    bool ok;
    QByteArray output;
};
QString terminalSocket();
TerminalResult terminalCommand(QStringList args);
QVariantMap terminalSnapshot(const QString& id, bool withText = true);
}
