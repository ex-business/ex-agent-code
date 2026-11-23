#include "history_context.h"
#include "infrastructure/storage/conversation_image.h"
#include <QFileInfo>
namespace exagent {
QVariantMap historyContext(const ChatSession &s, const QString &directory, int first, int last)
{
    QString text;
    QStringList images, names;
    const auto root = QFileInfo(directory + "/attachments").canonicalFilePath();
    int turn = -1;
    if (first < 0 || first > last)
        return { { "error", QStringLiteral("请选择有效的历史回合。") } };
    for (const auto &m : s.messages) {
        if (turn == -1 || m.startsTurn || m.role == MessageRole::User)
            ++turn;
        if (turn > last)
            break;
        if (turn < first)
            continue;
        const auto role = m.role == MessageRole::User ? QStringLiteral("用户")
            : m.role == MessageRole::Assistant        ? QStringLiteral("助手")
            : m.role == MessageRole::Tool             ? QStringLiteral("工具")
                                                      : QStringLiteral("记录");
        text += role + " · " + m.title + '\n' + m.content + "\n\n";
        if (text.size() > 48000)
            return { { "error", QStringLiteral("所选历史过长，请缩小回合范围。") } };
        for (const auto &value : m.attachments) {
            const auto a = value.toMap();
            const auto path
                = QFileInfo(conversationImagePath(directory, a.value("asset").toString())).canonicalFilePath();
            if (root.isEmpty() || path.isEmpty() || !path.startsWith(root + '/') || !QFileInfo(path).isFile())
                return { { "error", QStringLiteral("所选回合含缺失或未归档的图片，请另选回合。") } };
            if (images.contains(path))
                continue;
            if (images.size() == 8)
                return { { "error", QStringLiteral("每次最多选择八张历史图片。") } };
            images.append(path);
            names.append(a.value("name").toString());
        }
    }
    if (last > turn)
        return { { "error", QStringLiteral("历史回合已变化，请重新选择。") } };
    return { { "text", text }, { "images", images }, { "names", names }, { "first", first }, { "last", last } };
}
}
