#include "http_server.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTimer>
#include <memory>
namespace exagent::mobile {
namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(65537);
    return bytes.size() <= 65536 ? bytes : QByteArray{};
}
void reply(QTcpSocket *socket, const Response &r) {
    QByteArray data =
        "HTTP/1.1 " + QByteArray::number(r.status) +
        " Response\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: "
        "nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; "
        "img-src 'self' blob:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'; object-src 'none'\r\nContent-Type: " +
        r.type + "\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\n\r\n" + r.body;
    socket->write(data);
    socket->disconnectFromHost();
}
} // namespace
bool HttpServer::start(const QString &address, int port, const QString &certificate, const QString &key,
                       QString *error) {
    stop();
    const QHostAddress ip(address);
    if (ip.protocol() != QAbstractSocket::IPv4Protocol || !QNetworkInterface::allAddresses().contains(ip) ||
        port < 1 || port > 65535) {
        *error = QStringLiteral("请选择本机具体IPv4地址及1至65535端口。");
        return false;
    }
    const bool tls = !certificate.isEmpty() || !key.isEmpty();
    // 明文只用于本机预览，向手机提供访问必须显式配置TLS，不能默默将输入权限暴露到局域网。
    if (!tls && ip != QHostAddress(QHostAddress::LocalHost)) {
        *error = QStringLiteral("非回环移动访问必须配置TLS证书和私钥。");
        return false;
    }
    if (tls) {
        if (!QSslSocket::supportsSsl()) {
            *error = QStringLiteral("当前Qt运行环境不能提供TLS。");
            return false;
        }
        const QFileInfo info(key);
        const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                             QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        const auto certs = QSslCertificate::fromData(read(certificate));
        const auto bytes = read(key);
        QSslKey privateKey(bytes, QSsl::Rsa);
        if (privateKey.isNull())
            privateKey = QSslKey(bytes, QSsl::Ec);
        if (certs.isEmpty() || privateKey.isNull() || !info.isFile() || (info.permissions() & exposed) ||
            certs.first().expiryDate() < QDateTime::currentDateTimeUtc()) {
            *error = QStringLiteral("TLS证书、私钥或私钥文件权限无效。");
            return false;
        }
        auto *server = new QSslServer(this);
        auto config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificateChain(certs);
        config.setPrivateKey(privateKey);
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        config.setProtocol(QSsl::TlsV1_2OrLater);
        server->setSslConfiguration(config);
        server->setHandshakeTimeout(5000);
        connect(server, &QSslServer::startedEncryptionHandshake, this, [this](QSslSocket *socket) {
            if (m_handshakes.size() >= 16) {
                socket->abort();
                return;
            }
            m_handshakes.insert(socket);
            connect(socket, &QSslSocket::encrypted, this, [this, socket] { m_handshakes.remove(socket); });
            connect(socket, &QObject::destroyed, this, [this, socket] { m_handshakes.remove(socket); });
        });
        m_server = server;
    } else
        m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(16);
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this, [this] { accept(); });
    if (!m_server->listen(ip, quint16(port))) {
        *error = QStringLiteral("移动接入端口无法监听：") + m_server->errorString();
        stop();
        return false;
    }
    m_origin = (tls ? "https://" : "http://") + address +
               ((tls && port == 443) || (!tls && port == 80) ? QString{} : ":" + QString::number(port));
    return true;
}
void HttpServer::stop() {
    if (m_server) {
        disconnect(m_server, nullptr, this, nullptr);
        m_server->close();
    }
    for (auto *socket : m_sockets) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
    m_buffers.clear();
    m_sockets.clear();
    m_handshakes.clear();
    delete m_server;
    m_server = nullptr;
    m_origin.clear();
}
void HttpServer::accept() {
    while (auto *socket = m_server->nextPendingConnection()) {
        if (m_sockets.size() >= 16) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_sockets.insert(socket);
        m_buffers.insert(socket, {});
        QTimer::singleShot(5000, socket, [socket] { socket->abort(); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            m_sockets.remove(socket);
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (!m_buffers.contains(socket))
                return;
            auto &bytes = m_buffers[socket];
            bytes.append(socket->readAll());
            auto fail = [this, socket] {
                m_buffers.remove(socket);
                reply(socket, {400, "text/plain; charset=utf-8", "请求无效。"});
            };
            if (bytes.size() > 73728) {
                fail();
                return;
            }
            const auto end = bytes.indexOf("\r\n\r\n");
            if (end < 0) {
                if (bytes.size() > 8192)
                    fail();
                return;
            }
            if (end > 8192) {
                fail();
                return;
            }
            const auto lines = bytes.left(end).split('\n'), start = lines.value(0).trimmed().split(' ');
            if (start.size() != 3 || start[2] != "HTTP/1.1" || (start[0] != "GET" && start[0] != "POST")) {
                fail();
                return;
            }
            Request r;
            r.method = start[0];
            r.path = start[1];
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const auto line = lines[i].trimmed();
                const auto colon = line.indexOf(':');
                if (colon < 1) {
                    fail();
                    return;
                }
                const auto key = line.left(colon).toLower();
                if (r.headers.contains(key)) {
                    fail();
                    return;
                }
                r.headers.insert(key, line.mid(colon + 1).trimmed());
            }
            if (r.headers.contains("transfer-encoding") ||
                r.headers.value("host") != m_origin.section("//", 1).toLatin1()) {
                fail();
                return;
            }
            bool ok = true;
            const auto raw = r.headers.value("content-length");
            const auto length = raw.isEmpty() ? 0 : raw.toLongLong(&ok);
            if (!ok || length < 0 || length > 65536 || (r.method == "GET" && length != 0)) {
                fail();
                return;
            }
            if (bytes.size() < end + 4 + length)
                return;
            if (bytes.size() != end + 4 + length) {
                fail();
                return;
            }
            r.body = bytes.mid(end + 4);
            m_buffers.remove(socket);
            if (r.method == "POST" &&
                (r.headers.value("content-type").split(';').first().trimmed() != "application/json" ||
                 (!r.headers.value("origin").isEmpty() &&
                  r.headers.value("origin") != m_origin.toLatin1()))) {
                reply(socket, {403, "text/plain; charset=utf-8", "来源不允许。"});
                return;
            }
            reply(socket, handle ? handle(r) : Response{503, "text/plain", "Unavailable"});
        });
        // TLS完成时请求可能已经缓存在socket中，补一次解析唤醒，不等待新的网络数据。
        if (socket->bytesAvailable() > 0)
            QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
    }
}
} // namespace exagent::mobile
