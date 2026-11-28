#pragma once
#include <QJsonObject>
#include <QObject>
namespace exagent::agent {
enum AccountKind {
  LocalChat = 0,
  SystemCodex = 1,
  DedicatedCodex = 2,
  OpenAIResponses = 3,
  PiNative = 4,
  ClaudeNative = 5
};
// 适配器只发布统一事件；聊天、审批和持久化不依赖具体模型协议。
class RuntimeRun : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void interrupt() = 0;
  virtual bool ownsSession(const QString &) const = 0;
  virtual bool decide(const QString &, bool) = 0;
  virtual bool steer(const QString &, const QString &, const QString &) {
    return false;
  }
signals:
  void produced(QJsonObject payload);
  void ended();
};
} // namespace exagent::agent
