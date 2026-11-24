#pragma once
#include <QStringList>
#include <QVariantMap>
namespace exagent {
struct TerminalResult {
    bool ok;
    QByteArray output;
};
TerminalResult terminalCommand(QStringList args);
QVariantMap terminalSnapshot(const QString& id);
}
