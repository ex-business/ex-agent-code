#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
namespace exagent::lsp {
struct Edit {
  int start, end;
  QString text;
};
int offset(const QString &source, const QJsonObject &position);
QJsonObject position(const QString &source, int offset);
bool edits(const QString &source, const QJsonArray &items,
           QVector<Edit> *result, QString *error);
} // namespace exagent::lsp
