#pragma once
#include <QVariantMap>
namespace exagent::hibernation {
QString directory(const QString& id);
QString installExtension();
void permit(const QString& id, int minutes, qint64 since);
void revoke(const QString& id);
bool sameTranscript(const QVariantMap& tab);
bool requested(const QVariantMap& tab);
QString frame(const QString& id);
}
