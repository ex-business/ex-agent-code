#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <functional>
namespace exagent {
// 一个工作区/执行器配置共用一个stdio服务；协议层不执行服务器命令或自动写文件。
class LspSession final : public QObject {
  Q_OBJECT
public:
  enum State { Stopped = 0, Starting = 1, Ready = 2, Failed = 3 };
  using Reply = std::function<void(QJsonValue, QString)>;
  LspSession(QString root, QString program, QStringList arguments);
  ~LspSession() override;
  void start();
  int state() const { return status; }
  QString error() const { return message; }
  QJsonObject capabilities() const { return caps; }
  int syncKind() const;
  void notify(const QString &method, const QJsonObject &params);
  void request(const QString &method, const QJsonObject &params, QObject *owner,
               Reply reply);
signals:
  void changed();
  void ready();
  void diagnostics(QJsonObject params);

private:
  struct Pending {
    QPointer<QObject> owner;
    Reply reply;
    qint64 started;
  };
  void send(QJsonObject object);
  void read();
  void fail(const QString &reason);
  void dispatch(QJsonObject object);
  QString root, program, message;
  QStringList arguments;
  QProcess process;
  QByteArray input;
  int body = -1, status = Stopped, nextId = 0;
  QHash<int, Pending> pending;
  QJsonObject caps;
  QTimer timeout;
};
} // namespace exagent
