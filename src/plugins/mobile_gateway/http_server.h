#pragma once
#include <QHash>
#include <QSet>
#include <QSslSocket>
#include <QTcpServer>
#include <functional>
namespace exagent::mobile {
struct Request {
    QByteArray method, path;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};
struct Response {
    int status = 200;
    QByteArray type = "application/json; charset=utf-8", body;
};
class HttpServer final : public QObject {
  public:
    explicit HttpServer(QObject *parent = nullptr) : QObject(parent) {}
    ~HttpServer() override { stop(); }
    bool start(const QString &address, int port, const QString &certificate, const QString &key,
               QString *error);
    void stop();
    QString origin() const { return m_origin; }
    std::function<Response(const Request &)> handle;

  private:
    void accept();
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QSet<QTcpSocket *> m_sockets;
    QSet<QSslSocket *> m_handshakes;
    QString m_origin;
};
} // namespace exagent::mobile
