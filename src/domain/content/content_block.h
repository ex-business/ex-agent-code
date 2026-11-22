#pragma once
#include <QJsonObject>
#include <QString>

namespace exagent::content {
// 内容模式、生产来源和渲染类型分别建模，不能从渲染组件或任务状态反推来源。
enum class ContentMode : int { Static = 1, Dynamic = 2 };
enum class BlockSource : int { Resource = 1, Internal = 2, External = 3 };
enum class TaskState : int { Created = 0, Running = 1, Complete = 2, Failed = 3, Cancelled = 4, Interrupted = 5 };
enum class BlockKind : int { Text = 1, Code = 2, Terminal = 3, Chart = 4, Image = 5, ImageSequence = 6, Diff = 7 };
enum class EventKind : int { Created = 1, Patch = 2, Snapshot = 3 };
struct ContentBlock {
    QString id, taskId, conversationId, title, text;
    BlockKind kind = BlockKind::Text;
    // 兼容既有prepare调用默认外部动态任务；静态适配器必须显式声明自身模式和来源。
    TaskState state = TaskState::Created; // 仅动态任务有效，静态块不序列化taskState。
    int revision = 0;
    QJsonObject data;
    ContentMode mode = ContentMode::Dynamic;
    BlockSource source = BlockSource::External;
};
struct BlockEvent {
    // 由宿主适配器指定，绝不从外部payload读取来源身份。
    BlockSource source;
    EventKind kind;
    QJsonObject payload;
};
inline bool identifier(const QJsonValue &value) {
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 128) return false;
    for (auto c : text) if (!c.isLetterOrNumber() && c != '-' && c != '_' && c != '.') return false;
    return true;
}
inline bool integer(const QJsonValue &value, qint64 minimum, qint64 maximum) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return number >= double(minimum) && number <= double(maximum) && double(qint64(number)) == number;
}
}
