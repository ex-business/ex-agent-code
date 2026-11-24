#pragma once
#include <QStringList>
#include <QVariantList>
namespace exagent {
struct HistoryProvider {
    QString name, executable, directory, resumeFlag;
    bool resumeByFile;
};
QList<HistoryProvider> installedHistoryFormats();
QVariantList discoverSessions(const QString& workspace);
QString cliPath(const QString& name);
}
