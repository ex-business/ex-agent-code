#pragma once
#include "domain/agent/account_status.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>
namespace exagent::accounts {
QString reference(const QVariantMap &config);

class Profiles final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
  Q_PROPERTY(bool writable READ writable CONSTANT)
  Q_PROPERTY(bool usageReady READ usageReady NOTIFY changed)
  Q_PROPERTY(int coverage READ coverage NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit Profiles(QObject *parent = nullptr);
  ~Profiles() override;
  QVariantList rows() const;
  std::function<bool()> mayWrite;
  bool writable() const { return m_writable && (!mayWrite || mayWrite()); }
  bool usageReady() const { return m_loaded; }
  int coverage() const {
    return !m_healthy ? 3 : !m_loaded ? 0 : m_incomplete ? 2 : 1;
  }
  QString error() const { return m_error; }
  QJsonObject profile(const QString &id) const;
  QJsonObject route(const QString &id, const QString &model,
                    bool automatic) const;
  Q_INVOKABLE QString create(int kind, const QString &name,
                             const QString &credentialFile,
                             const QString &model, const QString &scope);
  Q_INVOKABLE bool resetAvailability(const QString &id);
  Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
  bool legacyApi(const QString &file, const QString &model);
  void observe(const QString &id, const QJsonObject &event);
  void usage(const QString &id, const QString &run, const QJsonObject &usage);
  QString home(const QString &id) const;
signals:
  void changed();
  void failure(QString reason);

private:
  bool save();
  bool put(QJsonObject p);
  void writeUsage();
  void addTotal(const QJsonObject &row, int direction);
  QJsonObject m_profiles, m_records;
  struct Total {
    qint64 input = 0, output = 0, tokens = 0;
    int runs = 0, inputs = 0, outputs = 0, totals = 0;
  };
  QHash<QString, Total> m_totals;
  QString m_dir, m_error;
  std::unique_ptr<QLockFile> m_lock;
  bool m_writable = false, m_loaded = false, m_dirty = false, m_healthy = true,
       m_incomplete = false;
  QFutureWatcher<QJsonObject> m_reader;
  QFutureWatcher<bool> m_writer;
  QTimer m_flush;
};
} // namespace exagent::accounts
