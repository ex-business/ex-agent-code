#pragma once

#include <QString>

namespace exagent {

// 引擎能力独立于聊天视图；后续 Codex/Pi 适配器通过此边界接入。
struct AgentCapabilities {
    bool structuredMessages = false;
    bool cancellation = false;
    bool approvals = false;
};

class AgentProvider {
public:
    virtual ~AgentProvider() = default;
    virtual QString providerId() const = 0;
    virtual AgentCapabilities capabilities() const = 0;
    virtual void submit(const QString &sessionId, const QString &message) = 0;
    virtual void cancel(const QString &sessionId) = 0;
};

} // namespace exagent
