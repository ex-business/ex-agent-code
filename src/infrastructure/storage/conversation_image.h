#pragma once
#include <QVariantMap>

namespace exagent {
struct ImageImport {
    QVariantMap attachment;
    QString error;
};
// 存储完整图像，展示尺寸由 Qt 委托决定；摘要文件名使相同内容可安全复用。
ImageImport importConversationImage(const QString &source, const QString &directory, const QString &name);
QString conversationImagePath(const QString &directory, const QString &asset);
}
