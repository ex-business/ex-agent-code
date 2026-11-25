#pragma once

namespace exagent::mail {
// 受理只表示引擎确认了指定回合；断线后不得猜测是否已消费。
enum Delivery { Queued = 0, Sending = 1, Accepted = 2, Rejected = 3, Unconfirmed = 4 };
inline constexpr int MaxText = 8192;
}
