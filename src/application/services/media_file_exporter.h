#pragma once
#include <QString>
namespace exagent {
// 授权在控制器完成；此模块仅负责后台有界复制与错误诊断。
class MediaFileExporter {
public:
    static QString copyOriginal(const QString &source, const QString &destination);
};
}
