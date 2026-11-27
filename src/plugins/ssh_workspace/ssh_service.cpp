#include "ssh_service.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <memory>
namespace exagent::ssh {
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QJsonObject find(const QJsonArray &items, const QString &key) {
    for (const auto &v : items)
        if (v.toObject()["id"] == key)
            return v.toObject();
    return {};
}
QJsonArray replace(QJsonArray items, const QJsonObject &item) {
    for (qsizetype i = 0; i < items.size(); ++i)
        if (items[i].toObject()["id"] == item["id"]) {
            items[i] = item;
            return items;
        }
    items.append(item);
    return items;
}
bool text(const QJsonValue &v, int limit) {
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= limit &&
           !v.toString().contains(QChar::Null);
}
QJsonObject page(const QJsonArray &items, const QJsonObject &params, QString *error) {
    auto result = control::paginate(items, params, error);
    if (!error->isEmpty())
        return {};
    QJsonArray bounded;
    qsizetype bytes = 0;
    const auto rows = result["items"].toArray();
    for (const auto &value : rows) {
        const auto size = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).size();
        if (!bounded.isEmpty() && bytes + size > 200 * 1024)
            break;
        bounded.append(value);
        bytes += size;
    }
    if (bounded.size() < rows.size())
        result.insert("next", bounded.last().toObject()["id"]);
    result.insert("items", bounded);
    return result;
}
} // namespace
Service::Service(control::Host &host) : QObject(&host), m_host(host) {
    m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ssh/registry.json";
    m_data = {{"version", 1}, {"hosts", QJsonArray{}}, {"workspaces", QJsonArray{}}, {"runs", QJsonArray{}}};
    QFile bridge(":/ssh-workspace/bridge.py");
    if (bridge.open(QIODevice::ReadOnly))
        m_bridge = QString::fromUtf8(bridge.readAll());
    QFile file(m_file);
    if (QFileInfo(m_file).isSymLink())
        m_valid = false;
    else if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            m_valid = false;
        else {
            const auto bytes = file.read(4 * 1024 * 1024 + 1);
            const auto data = QJsonDocument::fromJson(bytes).object();
            m_valid = bytes.size() <= 4 * 1024 * 1024 && file.error() == QFileDevice::NoError &&
                      data["version"].toDouble(-1) == 1 && data["hosts"].isArray() &&
                      data["workspaces"].isArray() && data["runs"].isArray();
            if (m_valid)
                m_data = data;
        }
    }
    const QRegularExpression alias("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    QSet<QString> keys;
    for (const auto &v : m_data["hosts"].toArray()) {
        const auto h = v.toObject();
        if (QUuid(h["id"].toString()).isNull() || keys.contains(h["id"].toString()) ||
            !alias.match(h["alias"].toString()).hasMatch() || !text(h["name"], 80) ||
            !h["config"].isString() || !h["storage"].isString())
            m_valid = false;
        keys.insert(h["id"].toString());
    }
    for (const auto &kind : {"workspaces", "runs"})
        for (const auto &v : m_data[kind].toArray()) {
            const auto row = v.toObject();
            if (QUuid(row["id"].toString()).isNull() || keys.contains(row["id"].toString()) ||
                !ipc::integer(row["state"], 0, 4))
                m_valid = false;
            keys.insert(row["id"].toString());
            if (QString(kind) == "workspaces" &&
                (!text(row["path"], 4096) || !row["path"].toString().startsWith('/') ||
                 this->host(row["host"].toString()).isEmpty()))
                m_valid = false;
            if (QString(kind) == "runs" &&
                (workspace(row["workspace"].toString()).isEmpty() || !text(row["script"], 65536)))
                m_valid = false;
        }
    const QList<QPair<QString, QSet<QString>>> methods{{"hosts", {"limit", "after"}},
                                                       {"add", {"name", "alias", "config", "storage"}},
                                                       {"probe", {"host"}},
                                                       {"workspaces", {"limit", "after"}},
                                                       {"bind", {"host", "project", "path"}},
                                                       {"check", {"workspace"}},
                                                       {"worktree", {"workspace", "branch", "start"}},
                                                       {"run", {"workspace", "script"}},
                                                       {"runs", {"limit", "after"}},
                                                       {"read", {"run", "offset"}},
                                                       {"request", {"id"}},
                                                       {"disconnect", {"host"}}};
    for (const auto &m : methods)
        host.add("ssh." + m.first, m.second,
                 [this, a = m.first](const QJsonObject &p, QString *e) { return execute(a, p, e); });
    host.chat.setSsh(this);
}
Service::~Service() {
    // 退出宿主只结束本机SSH传输，已经脱离SSH的远程任务仍由远端运行目录管理。
    for (auto *process : m_processes) {
        disconnect(process, nullptr, this, nullptr);
        process->kill();
    }
}
QVariantMap Service::operate(const QString &a, const QVariantMap &p) {
    QString e;
    const auto r = m_host.invoke("ssh." + a, QJsonObject::fromVariantMap(p), &e);
    return e.isEmpty() ? QVariantMap{{"ok", true}, {"result", r.toVariantMap()}}
                       : QVariantMap{{"ok", false}, {"error", e}};
}
QJsonObject Service::host(const QString &key) const { return find(m_data["hosts"].toArray(), key); }
QJsonObject Service::workspace(const QString &key) const { return find(m_data["workspaces"].toArray(), key); }
bool Service::save(const QJsonObject &data, QString *e) {
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (!m_valid || QFileInfo(m_file).isSymLink() || bytes.size() > 4 * 1024 * 1024 ||
        !QDir().mkpath(QFileInfo(m_file).absolutePath())) {
        *e = QStringLiteral("SSH存储不可写或超过4MiB，原资料保留。");
        return false;
    }
    QSaveFile file(m_file);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        *e = QStringLiteral("SSH状态保存失败，原资料保留。");
        return false;
    }
    m_data = data;
    return true;
}
QString Service::request(const QJsonObject &h, QJsonObject p,
                         std::function<void(const QJsonObject &, QString &)> done, QString *e) {
    if (h.isEmpty() || m_bridge.isEmpty() || m_processes.size() >= 8) {
        *e = QStringLiteral("SSH主机不可用或连接任务过多。");
        return {};
    }
    const auto config = h["config"].toString();
    if (!config.isEmpty() && (!QFileInfo(config).isAbsolute() || !QFileInfo(config).isFile())) {
        *e = QStringLiteral("OpenSSH配置文件不可读取。");
        return {};
    }
    const auto executable = QStandardPaths::findExecutable("ssh");
    if (executable.isEmpty()) {
        *e = QStringLiteral("未找到OpenSSH客户端。");
        return {};
    }
    auto *process = new QProcess(this);
    QStringList args;
    if (!config.isEmpty())
        args << "-F" << config;
    args << "-o"
         << "BatchMode=yes"
         << "-o"
         << "StrictHostKeyChecking=yes"
         << "-o"
         << "ForwardAgent=no"
         << "-o"
         << "ClearAllForwardings=yes"
         << "-o"
         << "RequestTTY=no"
         << "-o"
         << "ControlMaster=no"
         << "-o"
         << "ControlPath=none"
         << "-o"
         << "ConnectTimeout=10"
         << "-o"
         << "ServerAliveInterval=10"
         << "-o"
         << "ServerAliveCountMax=2" << h["alias"].toString();
    auto script = m_bridge;
    script.replace('\'', "'\\''");
    args << ("python3 -c '" + script + "'");
    p.insert("storage", h["storage"]);
    const auto payload = QJsonDocument(p).toJson(QJsonDocument::Compact);
    if (payload.size() > 262144) {
        *e = QStringLiteral("SSH请求超过容量。");
        process->deleteLater();
        return {};
    }
    const auto key = id();
    m_requests.insert(key, {{"id", key}, {"state", 0}, {"host", h["id"]}});
    m_processes.insert(key, process);
    auto output = std::make_shared<QByteArray>(), errors = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, process, key, output, errors, finished, done](QString error) {
        if (*finished)
            return;
        *finished = true;
        const auto response = QJsonDocument::fromJson(*output).object();
        if (error.isEmpty() && (process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0))
            error =
                QStringLiteral("SSH连接失败，远程运行状态待核对。") + QString::fromUtf8(*errors).left(2048);
        if (error.isEmpty() && response["ok"] != true)
            error = response["error"].toString(QStringLiteral("SSH返回无效，状态待核对。"));
        const auto result = response["result"].toObject();
        done(result, error);
        m_requests[key] = {{"id", key},
                           {"state", error.isEmpty() ? 2 : 3},
                           {"host", m_requests.value(key)["host"]},
                           {"result", result},
                           {"error", error}};
        m_finished.append(key);
        while (m_finished.size() > 128)
            m_requests.remove(m_finished.takeFirst());
        m_processes.remove(key);
        process->deleteLater();
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output, finish] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 524288) {
            process->kill();
            finish(QStringLiteral("SSH响应超过512KiB，远程状态待核对。"));
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] {
        const auto data = process->readAllStandardError();
        if (errors->size() < 8192)
            errors->append(data.left(8192 - errors->size()));
    });
    connect(process, &QProcess::started, this, [process, payload] {
        process->write(payload);
        process->closeWriteChannel();
    });
    connect(process, &QProcess::finished, this, [process, output, finish](int, QProcess::ExitStatus) {
        output->append(process->readAllStandardOutput());
        finish(output->size() > 524288 ? QStringLiteral("SSH响应超过512KiB，状态待核对。") : QString{});
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(QStringLiteral("SSH客户端无法启动。"));
    });
    QTimer::singleShot(45000, process, [process, finish] {
        process->kill();
        finish(QStringLiteral("SSH请求超时；远程任务可能仍在运行，请用运行ID重连核对。"));
    });
    process->setProgram(executable);
    process->setArguments(args);
    process->setProperty("sshHost", h["id"].toString());
    process->start();
    return key;
}
QJsonObject Service::execute(const QString &a, const QJsonObject &p, QString *e) {
    if (!m_valid) {
        *e = QStringLiteral("SSH注册表无效，已暂停写入并保留原文件。");
        return {};
    }
    if (a == "hosts")
        return page(m_data["hosts"].toArray(), p, e);
    if (a == "workspaces")
        return page(m_data["workspaces"].toArray(), p, e);
    if (a == "runs") {
        QJsonArray rows;
        for (const auto &v : m_data["runs"].toArray()) {
            auto r = v.toObject();
            r.insert("scriptPreview", r.take("script").toString().left(200));
            rows.append(r);
        }
        return page(rows, p, e);
    }
    if (a == "request") {
        const auto r = m_requests.value(p["id"].toString());
        if (r.isEmpty())
            *e = QStringLiteral("SSH请求记录已过期，请重新查询工作区或运行ID。");
        return r;
    }
    if (a == "add") {
        const QRegularExpression alias("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
        const auto config = p["config"].toString(), storage = p["storage"].toString();
        if ((!p["config"].isUndefined() && !p["config"].isString()) ||
            (!p["storage"].isUndefined() && !p["storage"].isString()) ||
            !text(p["name"], 80) || !alias.match(p["alias"].toString()).hasMatch() ||
            (!config.isEmpty() && (!QFileInfo(config).isAbsolute() || !QFileInfo(config).isFile())) ||
            (!storage.isEmpty() &&
             (!storage.startsWith('/') || storage.size() > 4096 || storage.contains(QChar::Null)))) {
            *e = QStringLiteral("名称、SSH别名、配置文件或远程存储路径无效。");
            return {};
        }
        for (const auto &v : m_data["hosts"].toArray())
            if (v.toObject()["alias"] == p["alias"] && v.toObject()["config"].toString() == config) {
                *e = QStringLiteral("此SSH配置别名已经保存。");
                return {};
            }
        QJsonObject h{{"id", id()},       {"name", p["name"]},  {"alias", p["alias"]},
                      {"config", config}, {"storage", storage}, {"state", 0}};
        auto next = m_data;
        auto hosts = next["hosts"].toArray();
        hosts.append(h);
        next.insert("hosts", hosts);
        if (!save(next, e))
            return {};
        return h;
    }
    if (a == "disconnect") {
        const auto h = host(p["host"].toString());
        if (h.isEmpty()) {
            *e = QStringLiteral("SSH主机不存在。");
            return {};
        }
        for (auto *process : m_processes)
            if (process->property("sshHost").toString() == p["host"].toString())
                process->kill();
        return {{"host", p["host"]}, {"state", 2}};
    }
    if (a == "probe") {
        const auto h = host(p["host"].toString());
        const auto key = request(
            h, {{"action", "probe"}},
            [this, h](const QJsonObject &r, QString &error) {
                auto row = host(h["id"].toString());
                row.insert("state", error.isEmpty() ? 1 : 2);
                row.insert("detail", error);
                row.insert("capabilities", r);
                auto next = m_data;
                next.insert("hosts", replace(next["hosts"].toArray(), row));
                QString reason;
                if (!save(next, &reason))
                    error = reason;
            },
            e);
        return key.isEmpty() ? QJsonObject{} : QJsonObject{{"request", key}};
    }
    if (a == "bind" || a == "check" || a == "worktree") {
        auto row = a == "bind" ? QJsonObject{{"id", id()},
                                             {"host", p["host"]},
                                             {"project", p["project"]},
                                             {"path", p["path"]},
                                             {"state", 0}}
                               : workspace(p["workspace"].toString());
        if (row.isEmpty() || host(row["host"].toString()).isEmpty() ||
            !m_host.chat.projects()->catalog.find(row["project"].toString()) || !text(row["path"], 4096) ||
            !row["path"].toString().startsWith('/')) {
            *e = QStringLiteral("远程主机、项目或绝对路径无效。");
            return {};
        }
        QJsonObject params{{"action", "inspect"}, {"path", row["path"]}};
        if (a == "worktree") {
            if (row["state"].toInt() != 1 || !text(p["branch"], 200) || !text(p["start"], 200)) {
                *e = QStringLiteral("源工作区未就绪或分支、起点无效。");
                return {};
            }
            row.insert("source", row["path"]);
            row.insert("id", id());
            row.insert("state", 0);
            row.insert("branch", p["branch"]);
            row.insert("start", p["start"]);
            row.insert("kind", 1);
        }
        // 重试保留同一工作树意图ID，连接中断后不盲目新建分支。
        if (row["kind"].toInt() == 1)
            params = {{"action", "worktree"},
                      {"id", row["id"]},
                      {"path", row["source"]},
                      {"branch", row["branch"]},
                      {"start", row["start"]}};
        auto next = m_data;
        next.insert("workspaces", replace(next["workspaces"].toArray(), row));
        if (!save(next, e))
            return {};
        const auto workspaceId = row["id"].toString();
        const auto key = request(
            host(row["host"].toString()), params,
            [this, workspaceId](const QJsonObject &r, QString &error) {
                auto row = workspace(workspaceId);
                row.insert("state", error.isEmpty() ? 1 : 2);
                row.insert("detail", error);
                if (error.isEmpty())
                    for (auto it = r.begin(); it != r.end(); ++it)
                        row.insert(it.key(), it.value());
                auto next = m_data;
                next.insert("workspaces", replace(next["workspaces"].toArray(), row));
                QString reason;
                if (!save(next, &reason))
                    error = reason;
            },
            e);
        return key.isEmpty() ? QJsonObject{} : QJsonObject{{"workspace", workspaceId}, {"request", key}};
    }
    if (a == "run" || a == "read") {
        auto run = a == "run" ? QJsonObject{{"id", id()},
                                            {"workspace", p["workspace"]},
                                            {"script", p["script"]},
                                            {"state", 0}}
                              : find(m_data["runs"].toArray(), p["run"].toString());
        const auto w = workspace(run["workspace"].toString());
        if (run.isEmpty() || w.isEmpty() || !text(run["script"], 65536) ||
            (a == "run" && w["state"].toInt() != 1)) {
            *e = QStringLiteral("远程工作区未就绪或运行参数无效。");
            return {};
        }
        if (a == "read" &&
            !ipc::integer(p["offset"].isUndefined() ? QJsonValue(0) : p["offset"], 0, 9007199254740991.0)) {
            *e = QStringLiteral("日志偏移必须为非负整数。");
            return {};
        }
        QJsonObject params{{"action", a == "run" ? "launch" : "read"},
                           {"id", run["id"]},
                           {"path", w["path"]},
                           {"script", run["script"]},
                           {"offset", p["offset"].toDouble(0)}};
        if (a == "run") {
            auto next = m_data;
            next.insert("runs", replace(next["runs"].toArray(), run));
            if (!save(next, e))
                return {};
        }
        const auto runId = run["id"].toString();
        const auto key = request(
            host(w["host"].toString()), params,
            [this, runId](const QJsonObject &r, QString &error) {
                auto run = find(m_data["runs"].toArray(), runId);
                run.insert("state", error.isEmpty() ? r["state"].toInt(4) : 4);
                run.insert("detail", error.isEmpty() ? r["detail"].toString() : error);
                if (r.contains("exitCode"))
                    run.insert("exitCode", r["exitCode"]);
                auto next = m_data;
                next.insert("runs", replace(next["runs"].toArray(), run));
                QString reason;
                if (!save(next, &reason))
                    error = reason;
            },
            e);
        return key.isEmpty() ? QJsonObject{} : QJsonObject{{"run", runId}, {"request", key}};
    }
    *e = QStringLiteral("未知SSH操作。");
    return {};
}
} // namespace exagent::ssh
