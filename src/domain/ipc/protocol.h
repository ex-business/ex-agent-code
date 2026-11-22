#pragma once
#include <QJsonObject>
#include <QString>
#include <QUuid>

namespace exagent::ipc {
inline constexpr int Version = 1;
inline constexpr quint32 MaxFrame = 1024 * 1024;
inline constexpr qint64 MaxQueue = 4 * 1024 * 1024;
inline constexpr int MaxPending = 64;
inline constexpr int EventWindow = 32;
inline constexpr int ReplayHistory = 128;
enum class ServiceState : int { Stopped = 0, Starting = 1, Negotiating = 2, Ready = 3, Reconnecting = 4, Failed = 5 };
enum class TaskState : int { Created = 0, Running = 1, Complete = 2, Failed = 3, Cancelled = 4, Interrupted = 5 };
enum class BlockKind : int { Text = 1, Code = 2, Terminal = 3, Chart = 4, Image = 5, ImageSequence = 6, Diff = 7 };
inline QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
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
