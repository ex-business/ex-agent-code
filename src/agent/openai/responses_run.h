#pragma once
#include "agent/runtime_run.h"
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
namespace exagent::openai {
class ResponsesRun final : public agent::RuntimeRun {
  Q_OBJECT
public:
  explicit ResponsesRun(QJsonObject spec, QObject *parent = nullptr,
                        QNetworkAccessManager *network = nullptr);
  void start() override;
  void interrupt() override;
  bool ownsSession(const QString &session) const override {
    return m_spec["conversationId"] == session;
  }
  bool decide(const QString &token, bool allow) override;

private:
  void send(QJsonArray input);
  void receive();
  void consumeEvent(const QJsonObject &e);
  void complete();
  void nextTool();
  void finish(int state, const QString &detail);
  void publish(QJsonObject e);
  void flush();
  QJsonObject m_spec, m_response;
  QNetworkAccessManager *m_network;
  QPointer<QNetworkReply> m_reply;
  QByteArray m_key, m_buffer;
  QTimer m_deadline, m_flush;
  QJsonArray m_calls, m_results;
  QString m_previous, m_token;
  QHash<QString, QString> m_text;
  QSet<QString> m_streamed;
  QJsonObject m_usage;
  QStringList m_order;
  int m_rounds = 0, m_index = 0;
  qsizetype m_outputBytes = 0;
  bool m_done = false, m_completed = false;
};
} // namespace exagent::openai
