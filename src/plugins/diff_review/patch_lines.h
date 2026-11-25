#pragma once
#include <QVariantMap>
#include <QStringList>

namespace exagent::review {
struct PatchLines {
    // 每个可批注行记录旧、新行号，0 表示该侧不存在。
    QHash<int, QPair<int, int>> rows;
    QString signature;
};
PatchLines parsePatch(const QStringList &lines);
QString digest(const QByteArray &bytes);
}
