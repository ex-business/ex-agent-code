#pragma once
namespace exagent::sharing {
// 自有审计协议使用数字枚举；记录身份和对象ID，不记录提示、令牌及配对码。
enum class Event {
  Invite = 1,
  CancelInvite = 2,
  Pair = 3,
  Revoke = 4,
  Close = 5,
  Grant = 6,
  Reclaim = 7,
  Request = 8,
  Release = 9,
  Expire = 10
};
enum class Phase { Intent = 1, Completed = 2, Failed = 3 };
} // namespace exagent::sharing
