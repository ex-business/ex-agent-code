#include "conversation_image.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

namespace exagent {
QString conversationImagePath(const QString &directory, const QString &asset)
{
    static const QRegularExpression safe(QStringLiteral("^[0-9a-f]{64}$"));
    if (!safe.match(asset).hasMatch()) return {};
    return QDir(directory).filePath("attachments/" + asset);
}

ImageImport importConversationImage(const QString &source, const QString &directory, const QString &name)
{
    constexpr qint64 limit = 20 * 1024 * 1024;
    QFile input(source);
    if (!QFileInfo(source).isFile() || !input.open(QIODevice::ReadOnly) || input.size() > limit)
        return {{}, QStringLiteral("图片无法读取或超过 20 MiB。")};
    QByteArray bytes = input.read(limit + 1);
    if (bytes.size() != input.size() || bytes.size() > limit) return {{}, QStringLiteral("读取期间图片大小发生变化。")};
    return storeConversationImage(std::move(bytes), directory, name);
}

ImageImport storeConversationImage(QByteArray bytes, const QString &directory, const QString &name)
{
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto size = reader.size();
    if (!reader.canRead() || size.isEmpty() || size.width() > 16384 || size.height() > 16384
        || qint64(size.width()) * size.height() > 32 * 1024 * 1024)
        return {{}, QStringLiteral("不支持该图片格式或像素尺寸。")};
    const auto key = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const auto path = conversationImagePath(directory, key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {{}, QStringLiteral("无法创建会话附件目录。")};
    // 原子发布，重试可以覆盖相同摘要的完整字节；未写完的图片不会出现在时间线里。
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return {{}, QStringLiteral("无法保存图片。")};
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (output.write(bytes) != bytes.size() || !output.commit()) return {{}, QStringLiteral("图片写入失败。")};
    return {{{"asset", key}, {"url", QUrl::fromLocalFile(path).toString()}, {"name", name},
             {"width", size.width()}, {"height", size.height()}}, {}};
}
}
