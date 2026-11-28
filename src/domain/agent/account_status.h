#pragma once
namespace exagent::accounts {
// 自有账号可用性；不把API/CLI的外部状态字符串直接作为业务状态。
enum Status {
  Unverified = 0,
  Ready = 1,
  Limited = 2,
  Disabled = 3,
  NeedsLogin = 4
};
} // namespace exagent::accounts
