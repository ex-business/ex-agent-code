#pragma once
#include <QFile>
#include <QImage>
namespace exagent::workspace_image {
struct Metadata {
  QSize size;
  int codec = 0;
}; // 1 Qt 栅格、2 静态 SVG、3 系统 HEIC。
bool inspect(QFile &file, Metadata *metadata, QString *error);
QImage decode(QFile &file, const QSize &requested);
} // namespace exagent::workspace_image
