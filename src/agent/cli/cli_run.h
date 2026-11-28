#pragma once
#include "agent/codex/thread_input.h"
#include "agent/runtime_run.h"
#include <QProcess>
#include <QTimer>
namespace exagent::cli {
class ProcessScope;
class Run : public agent::RuntimeRun {
  Q_OBJECT
public:
  Run(QJsonObject spec, QObject *parent = nullptr);
  ~Run() override;
  void start() override;
  void interrupt() override;
  bool ownsSession(const QString &id) const override {
    return spec["conversationId"] == id;
  }
  bool decide(const QString &token, bool allow) override;

protected:
  virtual QStringList arguments() = 0;
  virtual void started() = 0;
  virtual void frame(const QJsonObject &) = 0;
  virtual void approvalReply(const QJsonObject &, bool) = 0;
  virtual void abortProtocol() = 0;
  void send(const QJsonObject &);
  void emitEvent(QJsonObject);
  void output(const QString &item, const QString &text, bool replace,
              const QString &title = {}, const QString &kind = "assistant");
  void requestApproval(const QString &id, const QJsonObject &request,
                       const QString &detail);
  void closeApproval(const QString &id);
  bool readyPrompt();
  void finish(int state, const QString &detail);
  void checkpoint();
  void flush();
  QJsonObject spec, source;
  QProcess process;
  QTimer deadline;
  bool stopping = false, done = false, prompted = false;

private:
  void read();
  void complete();
  std::unique_ptr<ProcessScope> scope;
  QByteArray buffer;
  QHash<QString, QJsonObject> approvals;
  QHash<QString, QJsonObject> outputs;
  QStringList order;
  QTimer publish;
  std::unique_ptr<codex::ThreadInputLock> lease;
  qint64 offset = 0;
  int finalState = 5;
  QString finalDetail;
  bool finishing = false;
};
agent::RuntimeRun *createPi(QJsonObject, QObject *);
agent::RuntimeRun *createClaude(QJsonObject, QObject *);
} // namespace exagent::cli
