#pragma once
#include <QJsonArray>
#include <QJsonObject>

namespace exagent::review {
QJsonObject codexEvidence(const QString &workspace, const QJsonArray &changes);
}
