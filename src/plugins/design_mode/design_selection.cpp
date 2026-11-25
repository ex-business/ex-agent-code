#include "design_selection.h"
#include "infrastructure/storage/conversation_image.h"
#include <QBuffer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <cmath>

namespace exagent::design {
namespace {
const QString draftKey = QStringLiteral("designSelection");
}

SelectionResult capture(const QVariantMap &data, const QImage &surface, const QString &directory)
{
    const auto box = data.value("rect").toList();
    QUrl page(data.value("pageUrl").toString());
    if ((page.scheme() != "http" && page.scheme() != "https") || page.host().isEmpty() ||
        surface.isNull() || box.size() != 4 || data.value("selector").toString().isEmpty())
        return {{}, QStringLiteral("无法定位所选元素或截取网页画面。")};
    double n[4];
    for (int i = 0; i < 4; ++i) {
        bool ok;
        n[i] = box[i].toDouble(&ok);
        if (!ok || !std::isfinite(n[i]) || n[i] < 0 || n[i] > 1)
            return {{}, QStringLiteral("元素超出网页可见范围。")};
    }
    const QRect crop = QRectF(n[0]*surface.width(), n[1]*surface.height(),
                              n[2]*surface.width(), n[3]*surface.height()).toAlignedRect().intersected(surface.rect());
    if (crop.isEmpty()) return {{}, QStringLiteral("所选元素没有可截取的像素。")};
    // 会话中只保存页面地址的路径部分，排除常含登录参数的查询串、片段和用户信息。
    QVariantMap context{{"page", page.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)}};
    for (const auto &key : {"tag","selector","markup","styles","neighbors","source"})
        context.insert(key,data.value(key));
    if (QJsonDocument::fromVariant(context).toJson(QJsonDocument::Compact).size() > 16384)
        return {{}, QStringLiteral("页面元素描述过大，请选择更小的元素。")};
    QByteArray bytes;
    QBuffer encoded(&bytes);
    encoded.open(QIODevice::WriteOnly);
    if (!surface.copy(crop).save(&encoded, "PNG")) return {{}, QStringLiteral("无法编码元素截图。")};
    auto image = storeConversationImage(bytes, directory, QStringLiteral("页面元素.png"));
    if (!image.error.isEmpty()) return {{}, image.error};
    image.attachment.remove("url");
    return {{{"context",context},{"image",image.attachment}}, {}};
}

QVariantMap pending(const ChatSession &session)
{
    return session.history.value(draftKey).toMap();
}
void setPending(ChatSession &session, const QVariantMap &selection)
{
    if (selection.isEmpty()) session.history.remove(draftKey);
    else session.history.insert(draftKey,selection);
}

QString apply(const QVariantMap &selection, const QString &directory, ChatMessage &message, QString &imagePath)
{
    if (selection.isEmpty()) return {};
    auto image = selection.value("image").toMap();
    imagePath = conversationImagePath(directory,image.value("asset").toString());
    const auto info = QFileInfo(imagePath);
    if (imagePath.isEmpty() || !info.isFile() || info.isSymLink() || !info.isReadable())
        return QStringLiteral("页面截图已不可用，请重新点选元素或从草稿移除附件。");
    image.insert("url",QUrl::fromLocalFile(imagePath).toString());
    message.attachments.append(image);
    message.activity.insert("designElement",selection.value("context"));
    return {};
}
}
