#include "mobile_service.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
namespace exagent::mobile {
namespace {
QString secret() {
    QByteArray out;
    for (int i = 0; i < 8; ++i)
        out += QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    return QString::fromLatin1(out);
}
QString digest(const QString &s) {
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}
Response json(QJsonObject value, int status = 200) {
    return {status, "application/json; charset=utf-8", QJsonDocument(value).toJson(QJsonDocument::Compact)};
}
Response failure(const QString &e, int status = 400) { return json({{"ok", false}, {"error", e}}, status); }

} // namespace
Service::Service(control::Host &h) : QObject(&h), m_host(h), m_server(this) {
    m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/mobile/registry.json";
    m_data = {{"version", 1}, {"devices", QJsonArray{}}, {"sends", QJsonArray{}}};
    QFile file(m_file);
    if (QFileInfo(m_file).isSymLink())
        m_valid = false;
    else if (file.exists()) {
        const QFileInfo info(m_file);
        const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                             QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        if (info.permissions() & exposed)
            m_valid = false;
#ifdef Q_OS_UNIX
        if (info.ownerId() != uint(getuid()))
            m_valid = false;
#endif
        if (!file.open(QIODevice::ReadOnly))
            m_valid = false;
        else {
            const auto bytes = file.read(4 * 1024 * 1024 + 1);
            auto root = QJsonDocument::fromJson(bytes).object();
            m_valid = m_valid && bytes.size() <= 4 * 1024 * 1024 && file.error() == QFileDevice::NoError &&
                      root["version"].toDouble(-1) == 1 && root["devices"].isArray() &&
                      root["sends"].isArray();
            if (m_valid)
                m_data = root;
        }
    }
    QSet<QString> ids;
    for (const auto &v : m_data["devices"].toArray()) {
        const auto d = v.toObject();
        if (QUuid(d["id"].toString()).isNull() || ids.contains(d["id"].toString()) ||
            d["hash"].toString().size() != 64 || !ipc::integer(d["scope"], 1, 2) ||
            !ipc::integer(d["state"], 1, 2) || d["session"].toString().isEmpty())
            m_valid = false;
        ids.insert(d["id"].toString());
    }
    for (const auto &v : m_data["sends"].toArray()) {
        const auto r = v.toObject();
        if (QUuid(r["id"].toString()).isNull() || !ids.contains(r["device"].toString()) ||
            !ipc::integer(r["state"], 0, 4) || !r["prompt"].isString())
            m_valid = false;
    }
    m_server.handle = [this](const Request &r) { return serve(r); };
    const QList<QPair<QString, QSet<QString>>> methods{
        {"status", {}},  {"start", {"address", "port", "certificate", "key"}},
        {"stop", {}},    {"offer", {"session", "scope"}},
        {"devices", {}}, {"revoke", {"id"}}};
    for (const auto &m : methods)
        h.add("mobile." + m.first, m.second,
              [this, a = m.first](const QJsonObject &p, QString *e) { return execute(a, p, e); });
    h.chat.setMobile(this);
}
void Service::setAppearance(const QVariantMap &colors, const QVariantMap &strings, const QString &language) {
    QJsonObject safeColors;
    for (const auto &key :
         {"background", "text", "surface", "border", "secondary", "code", "accent", "onAccent"})
        safeColors.insert(key, colors.value(key).toString());
    QJsonObject safeStrings;
    // 移动页面只导出自身使用的短界面文案，不向未配对设备发送整个插件语言包。
    QFile module(":/mobile/appearance.js");
    if (module.open(QIODevice::ReadOnly)) {
        const auto source = QString::fromUtf8(module.readAll());
        for (auto it = strings.begin(); it != strings.end(); ++it)
            if (it.key().size() <= 200 && it.value().toString().size() <= 1000 &&
                source.contains("'" + it.key() + "'"))
                safeStrings.insert(it.key(), it.value().toString());
    }
    m_appearance = {{"language", language}, {"colors", safeColors}, {"strings", safeStrings}};
}
QVariantMap Service::operate(const QString &a, const QVariantMap &p) {
    QString e;
    const auto r = m_host.invoke("mobile." + a, QJsonObject::fromVariantMap(p), &e);
    return e.isEmpty() ? QVariantMap{{"ok", true}, {"result", r.toVariantMap()}}
                       : QVariantMap{{"ok", false}, {"error", e}};
}
bool Service::save(const QJsonObject &data, QString *e) {
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (!m_valid || QFileInfo(m_file).isSymLink() || bytes.size() > 4 * 1024 * 1024 ||
        !QDir().mkpath(QFileInfo(m_file).absolutePath())) {
        *e = QStringLiteral("移动接入存储不可用或超过容量。");
        return false;
    }
    QSaveFile f(m_file);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) ||
        !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        f.write(bytes) != bytes.size() || !f.commit()) {
        *e = QStringLiteral("移动授权保存失败，原资料保留。");
        return false;
    }
    m_data = data;
    return true;
}
QJsonObject Service::execute(const QString &a, const QJsonObject &p, QString *e) {
    if (!m_valid) {
        *e = QStringLiteral("移动授权存储无效，已停止接入。");
        return {};
    }
    if (a == "status")
        return {{"state", m_server.origin().isEmpty() ? 0 : 1}, {"origin", m_server.origin()}};
    if (a == "start") {
        if (!ipc::integer(p["port"], 1, 65535) || !p["address"].isString() ||
            (!p["certificate"].isUndefined() && !p["certificate"].isString()) ||
            (!p["key"].isUndefined() && !p["key"].isString())) {
            *e = QStringLiteral("监听地址、端口或TLS配置无效。");
            return {};
        }
        m_offers.clear();
        if (!m_server.start(p["address"].toString(), p["port"].toInt(), p["certificate"].toString(),
                            p["key"].toString(), e))
            return {};
        return {{"origin", m_server.origin()}, {"state", 1}};
    }
    if (a == "stop") {
        m_server.stop();
        m_offers.clear();
        return {{"state", 0}};
    }
    if (a == "offer") {
        if (m_server.origin().isEmpty() || !ipc::integer(p["scope"], 1, 2)) {
            *e = QStringLiteral("请先启动移动接入并选择读取或发送权限。");
            return {};
        }
        if (m_host.invoke("sessions.show", {{"session", p["session"]}}, e).isEmpty())
            return {};
        const auto now = QDateTime::currentSecsSinceEpoch();
        for (const auto &key : m_offers.keys())
            if (m_offers[key]["expires"].toDouble() <= now)
                m_offers.remove(key);
        if (m_offers.size() >= 8) {
            *e = QStringLiteral("待配对授权过多，请等待其过期或停止接入。");
            return {};
        }
        const auto code = secret();
        const QJsonObject offer{{"session", p["session"]}, {"scope", p["scope"]}, {"expires", now + 300}};
        m_offers.insert(digest(code), offer);
        return {{"url", m_server.origin() + "/#" + code}, {"expires", now + 300}, {"scope", p["scope"]}};
    }
    if (a == "devices") {
        QJsonArray rows;
        for (const auto &v : m_data["devices"].toArray()) {
            auto d = v.toObject();
            d.remove("hash");
            rows.append(d);
        }
        return {{"items", rows}};
    }
    if (a == "revoke") {
        auto rows = m_data["devices"].toArray();
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto d = rows[i].toObject();
            if (d["id"] != p["id"])
                continue;
            d.insert("state", 2);
            rows[i] = d;
            auto next = m_data;
            next.insert("devices", rows);
            if (!save(next, e))
                return {};
            return {{"id", p["id"]}, {"state", 2}};
        }
        *e = QStringLiteral("设备不存在。");
        return {};
    }
    *e = QStringLiteral("未知移动接入操作。");
    return {};
}
Response Service::serve(const Request &r) {
    if (!m_valid)
        return failure(QStringLiteral("授权存储不可用。"), 503);
    if (r.method == "GET") {
        QString resource;
        QByteArray type;
        if (r.path == "/") {
            resource = ":/mobile/index.html";
            type = "text/html; charset=utf-8";
        } else if (r.path == "/client.js") {
            resource = ":/mobile/client.js";
            type = "text/javascript; charset=utf-8";
        } else if (r.path == "/appearance.js") {
            resource = ":/mobile/appearance.js";
            type = "text/javascript; charset=utf-8";
        } else if (r.path == "/appearance.json") {
            return json(m_appearance);
        } else if (r.path == "/style.css") {
            resource = ":/mobile/style.css";
            type = "text/css; charset=utf-8";
        } else
            return failure(QStringLiteral("未找到页面。"), 404);
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("客户端资源不可用。"), 503);
        return {200, type, file.readAll()};
    }
    const auto doc = QJsonDocument::fromJson(r.body);
    if (!doc.isObject())
        return failure(QStringLiteral("请求须为JSON对象。"));
    const auto p = doc.object();
    QString error;
    if (r.path == "/api/pair") {
        if (p.size() != 2 || !p["code"].isString() || !p["name"].isString() ||
            p["name"].toString().trimmed().isEmpty() || p["name"].toString().size() > 80)
            return failure(QStringLiteral("设备名称或配对码无效。"));
        const auto key = digest(p["code"].toString());
        const auto offer = m_offers.value(key);
        if (offer.isEmpty() || offer["expires"].toDouble() <= QDateTime::currentSecsSinceEpoch())
            return failure(QStringLiteral("配对码无效或已过期。"), 401);
        auto devices = m_data["devices"].toArray();
        if (devices.size() >= 32)
            return failure(QStringLiteral("设备资料已达32条，请在桌面检查。"));
        const auto token = secret();
        QJsonObject device{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                           {"name", p["name"]},
                           {"session", offer["session"]},
                           {"scope", offer["scope"]},
                           {"state", 1},
                           {"hash", digest(token)}};
        devices.append(device);
        auto next = m_data;
        next.insert("devices", devices);
        if (!save(next, &error))
            return failure(error, 503);
        m_offers.remove(key);
        device.remove("hash");
        return json({{"ok", true}, {"token", token}, {"device", device}});
    }
    const auto auth = r.headers.value("authorization");
    if (!auth.startsWith("Bearer ") || auth.size() != 71)
        return failure(QStringLiteral("请重新配对。"), 401);
    QJsonObject device;
    const auto hash = digest(QString::fromLatin1(auth.mid(7)));
    for (const auto &v : m_data["devices"].toArray())
        if (v.toObject()["hash"] == hash && v.toObject()["state"].toInt() == 1) {
            device = v.toObject();
            break;
        }
    if (device.isEmpty())
        return failure(QStringLiteral("设备授权已失效。"), 401);
    if (r.path == "/api/snapshot") {
        if (p.size() > 1 || (!p.isEmpty() && !p["before"].isString()))
            return failure(QStringLiteral("历史参数无效。"));
        auto result =
            m_host.chat.conversationSnapshot(device["session"].toString(), p["before"].toString(), &error);
        if (!error.isEmpty())
            return failure(error);
        result.insert("scope", device["scope"]);
        result.insert("appearance", m_appearance);
        return json({{"ok", true}, {"result", result}});
    }
    if (r.path == "/api/send") {
        if (device["scope"].toInt() != 2)
            return failure(QStringLiteral("此设备只有读取权限。"), 403);
        if (p.size() != 2 || QUuid(p["id"].toString()).isNull() || !p["prompt"].isString() ||
            p["prompt"].toString().trimmed().isEmpty() || p["prompt"].toString().size() > 16384)
            return failure(QStringLiteral("输入或请求ID无效。"));
        auto sends = m_data["sends"].toArray();
        for (const auto &v : sends) {
            const auto s = v.toObject();
            if (s["id"] != p["id"] || s["device"] != device["id"])
                continue;
            if (s["prompt"] != p["prompt"])
                return failure(QStringLiteral("同一请求ID不能发送不同内容。"), 409);
            return json({{"ok", true},
                         {"result", QJsonObject{{"id", s["id"]},
                                                {"state", s["state"].toInt() == 0 ? 4 : s["state"].toInt()},
                                                {"run", s["run"]}}}});
        }
        if (sends.size() >= 256)
            return failure(QStringLiteral("移动发送记录达到首版保留上限，请在桌面检查。"), 409);
        QJsonObject item{{"id", p["id"]}, {"device", device["id"]}, {"prompt", p["prompt"]}, {"state", 0}};
        sends.append(item);
        auto next = m_data;
        next.insert("sends", sends);
        if (!save(next, &error))
            return failure(error, 503);
        // 先保存发送意图；断线/重启不重放未确认输入，同ID查询只返回原结果。
        const auto result =
            m_host.invoke("sessions.send", {{"session", device["session"]}, {"prompt", p["prompt"]}}, &error);
        item.insert("state", error.isEmpty() ? 1 : 3);
        item.insert("run", result["run"]);
        item.insert("error", error);
        sends[sends.size() - 1] = item;
        next = m_data;
        next.insert("sends", sends);
        QString saveError;
        if (!save(next, &saveError))
            return failure(QStringLiteral("发送结果待核对，请保留请求ID并重连查询。"), 503);
        return json(
            {{"ok", true},
             {"result",
              QJsonObject{
                  {"id", item["id"]}, {"state", item["state"]}, {"run", item["run"]}, {"error", error}}}});
    }
    return failure(QStringLiteral("此移动接口未开放。"), 404);
}
} // namespace exagent::mobile
