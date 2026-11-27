#pragma once
namespace exagent::supervision {
// 状态按数字持久化；不确定表示旧执行仍需核对，不能自动重放。
enum State { Pending = 0, Decision = 1, Working = 2, Completed = 3, Failed = 4, Uncertain = 5 };
}
