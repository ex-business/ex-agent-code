#pragma once
#include "domain/content/content_block.h"
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
// v1线协议保留原名称与数字值，领域类型不再依赖传输模块。
using content::TaskState;
using content::BlockKind;
using content::identifier;
using content::integer;
inline QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
