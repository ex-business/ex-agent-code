#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace exagent {
// 每个内嵌或独立查看器拥有自己的资源租期，关闭原页签不使独立窗口失效。
class WorkspaceImageMonitor : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(WorkspaceImageMonitor)
  Q_PROPERTY(QString path MEMBER m_path NOTIFY inputChanged)
  Q_PROPERTY(QString rootPath MEMBER m_root NOTIFY inputChanged)
  Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(int state READ state NOTIFY changed)
public:
  enum State { Idle = 0, Ready = 1, Unavailable = 2 };
  Q_ENUM(State)
  explicit WorkspaceImageMonitor(QObject *parent = nullptr);
  ~WorkspaceImageMonitor() override;
  QVariantMap info() const { return m_info; }
  QString error() const { return m_error; }
  int state() const { return m_state; }
signals:
  void inputChanged();
  void changed();

private:
  void refresh();
  QString m_path, m_root, m_error;
  QVariantMap m_info;
  int m_state = Idle;
  QTimer m_delay;
  QFileSystemWatcher m_watch;
};
} // namespace exagent
