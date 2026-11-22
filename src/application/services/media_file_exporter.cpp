#include "media_file_exporter.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
namespace exagent {
QString MediaFileExporter::copyOriginal(const QString &source, const QString &destination)
{
    // 工作线程再次核对源和目标，不能覆盖同一原图或跟随已替换的源符号链接。
    if (QFileInfo(source).canonicalFilePath() != source || QFileInfo(destination).canonicalFilePath() == source)
        return QStringLiteral("图片路径已变化或目标与原图相同");
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return QStringLiteral("无法读取原图：") + input.errorString();
    if (input.size() > 64 * 1024 * 1024) return QStringLiteral("原图超过64MiB导出预算");
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) return QStringLiteral("无法写入目标：") + output.errorString();
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qint64 copied = 0;
    while (!input.atEnd()) {
        const auto bytes = input.read(256 * 1024);
        if (bytes.isEmpty()) return QStringLiteral("读取原图中断：") + input.errorString();
        copied += bytes.size();
        if (copied > 64 * 1024 * 1024) return QStringLiteral("原图复制期间超过64MiB导出预算");
        if (output.write(bytes) != bytes.size()) return QStringLiteral("目标写入不完整：") + output.errorString();
    }
    if (!output.commit()) return QStringLiteral("目标文件提交失败：") + output.errorString();
    return {};
}
}
