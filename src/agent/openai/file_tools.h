#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace exagent::openai {
QJsonArray fileTools(bool write);
struct FileResult {
  bool success;
  QString text;
};
FileResult fileTool(const QString &root, const QString &secretFile,
                    const QString &name, const QJsonObject &args, bool write);
} // namespace exagent::openai
