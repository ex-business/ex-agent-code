#include "bot_service.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
namespace exagent::bots {
namespace {
constexpr auto Plugin = "local-bots";
bool label(const QJsonValue &v, int maximum) {
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= maximum &&
           !v.toString().contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
bool role(const QJsonValue &v) {
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= 8192 &&
           !v.toString().contains(QChar::Null);
}
} // namespace
Service::Service(control::Host &host) : QObject(&host), m_host(host) {
    m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bots/registry.json";
    QFile file(m_file);
    if (QFileInfo(m_file).isSymLink())
        m_valid = false;
    else if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            m_valid = false;
        else {
            const auto bytes = file.read(4 * 1024 * 1024 + 1);
            const auto root = QJsonDocument::fromJson(bytes).object();
            m_valid = bytes.size() <= 4 * 1024 * 1024 && file.error() == QFileDevice::NoError &&
                      root["version"].toDouble(-1) == 1 && root["bots"].isArray();
            if (m_valid)
                m_bots = root["bots"].toArray();
        }
    }
    QSet<QString> ids, sessions;
    for (const auto &v : m_bots) {
        const auto b = v.toObject();
        if (QUuid(b["id"].toString()).isNull() || ids.contains(b["id"].toString()) || !label(b["name"], 64) ||
            !role(b["role"]) || b["host"] != "local" || b["agent"] != "codex" ||
            b["project"].toString().isEmpty() || !b["workspace"].isString() ||
            !ipc::integer(b["state"], Preparing, Paused) || !ipc::integer(b["revision"], 0, 2147483646) ||
            !ipc::integer(b["generation"], 0, 2147483645) || !b["sessions"].isArray() ||
            b["sessions"].toArray().size() > 256)
            m_valid = false;
        bool current = b["state"].toInt() == Preparing;
        for (const auto &s : b["sessions"].toArray()) {
            if (QUuid(s.toString()).isNull() || sessions.contains(s.toString()))
                m_valid = false;
            sessions.insert(s.toString());
            if (s == b["session"])
                current = true;
        }
        if (!current)
            m_valid = false;
        ids.insert(b["id"].toString());
    }
    const QList<QPair<QString, QSet<QString>>> methods{{"list", {"limit", "after"}},
                                                       {"create", {"name", "role", "project", "workspace"}},
                                                       {"get", {"id"}},
                                                       {"prepare", {"id"}},
                                                       {"update", {"id", "name", "role", "revision"}},
                                                       {"set", {"id", "state", "revision"}},
                                                       {"open", {"id"}},
                                                       {"new-chat", {"id", "revision"}},
                                                       {"send", {"id", "prompt"}}};
    for (const auto &m : methods)
        host.add("bots." + m.first, m.second,
                 [this, a = m.first](const QJsonObject &p, QString *e) { return execute(a, p, e); });
    host.chat.agent()->registerInputPolicy(
        Plugin, this,
        [this](const ChatSession &s, int c, QString &t, QString *e) { return policy(s, c, t, e); });
    connect(&host.chat, &ChatController::sessionChanged, this, &Service::changed);
    host.chat.setBots(this);
}
QVariantMap Service::operate(const QString &action, const QVariantMap &params) {
    QString e;
    const auto r = m_host.invoke("bots." + action, QJsonObject::fromVariantMap(params), &e);
    return e.isEmpty() ? QVariantMap{{"ok", true}, {"result", r.toVariantMap()}}
                       : QVariantMap{{"ok", false}, {"error", e}};
}
QVariantMap Service::current() const {
    const auto id =
        m_host.chat.agent()->sessionExtension(m_host.chat.currentSessionId(), Plugin).value("id").toString();
    if (id.isEmpty())
        return {};
    if (m_valid)
        for (const auto &v : m_bots)
            if (v.toObject()["id"] == id)
                return v.toObject().toVariantMap();
    return {{"id", id}, {"name", QStringLiteral("Bot 不可用")}, {"state", 3}};
}
bool Service::commit(const QJsonArray &bots, QString *e) {
    if (!m_valid || QFileInfo(m_file).isSymLink()) {
        *e = QStringLiteral("Bot 存储不可用，已有资料保留。");
        return false;
    }
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"bots", bots}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024 || !QDir().mkpath(QFileInfo(m_file).absolutePath())) {
        *e = QStringLiteral("Bot 存储超出容量或目录不能创建。");
        return false;
    }
    QSaveFile file(m_file);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        *e = QStringLiteral("Bot 保存失败，原状态保留。");
        return false;
    }
    m_bots = bots;
    emit changed();
    return true;
}
bool Service::busy(const QJsonObject &bot) const {
    for (const auto &s : bot["sessions"].toArray())
        if (m_host.chat.agent()->activeSession(s.toString()))
            return true;
    return false;
}
bool Service::policy(const ChatSession &s, int channel, QString &text, QString *e) const {
    const auto id = s.history.value("inputExtensions").toMap().value(Plugin).toMap().value("id").toString();
    if (!m_valid) {
        *e = QStringLiteral("Bot 注册表不可读取，已暂停此会话输入。");
        return false;
    }
    for (const auto &v : m_bots) {
        const auto b = v.toObject();
        if (b["id"] != id)
            continue;
        if (b["state"].toInt() != Active || !b["sessions"].toArray().contains(s.id) ||
            b["project"].toString() != s.projectId || b["workspace"].toString() != s.workspaceId) {
            *e = QStringLiteral("Bot 尚未就绪、已暂停或会话归属不一致。");
            return false;
        }
        // 原生终端暂不能保证每回合职责注入，首版明确拒绝接管 Bot 线程。
        if (channel == AgentController::TerminalInput) {
            *e = QStringLiteral("Bot 首版仅支持原生聊天输入。");
            return false;
        }
        for (const auto &other : b["sessions"].toArray())
            if (other != s.id && m_host.chat.agent()->activeSession(other.toString())) {
                *e = QStringLiteral("此 Bot 的另一个聊天仍在运行。");
                return false;
            }
        text = QStringLiteral("用户配置的 Bot：%1\n职责（不改变现有权限或审批规则）：\n%2\n\n用户输入：\n%3")
                   .arg(b["name"].toString(), b["role"].toString(), text);
        return true;
    }
    *e = QStringLiteral("此会话绑定的 Bot 不存在，禁止降级为普通聊天。");
    return false;
}
QJsonObject Service::prepare(const QString &id, QString *e) {
    for (qsizetype i = 0; i < m_bots.size(); ++i) {
        auto b = m_bots[i].toObject();
        if (b["id"] != id)
            continue;
        if (b["state"].toInt() != Preparing)
            return b;
        if (b["revision"].toInt() >= 2147483646) {
            *e = QStringLiteral("Bot 版本计数已达上限。");
            return {};
        }
        QString session = b["session"].toString();
        // 先找已绑定的会话，重试准备不重复创建已完成绑定的资源。
        if (session.isEmpty())
            for (const auto &v : m_host.chat.projects()->rows()) {
                const auto row = v.toMap(),
                           ext =
                               m_host.chat.agent()->sessionExtension(row.value("session").toString(), Plugin);
                if (row.value("kind").toInt() == ProjectNavigator::SessionRow && ext.value("id") == id &&
                    ext.value("generation").toInt() == b["generation"].toInt()) {
                    session = row.value("session").toString();
                    break;
                }
            }
        if (session.isEmpty()) {
            const auto result = m_host.invoke("sessions.create",
                                              {{"project", b["project"]}, {"workspace", b["workspace"]}}, e);
            if (!e->isEmpty())
                return {};
            session = result["id"].toString();
        }
        const auto target = m_host.invoke("sessions.show", {{"session", session}}, e);
        if (!e->isEmpty() || target["project"] != b["project"] ||
            target["workspace"].toString() != b["workspace"].toString()) {
            *e = QStringLiteral("Bot 准备会话的项目或工作区不一致。");
            return {};
        }
        if (!m_host.chat.agent()->setSessionExtension(
                session, Plugin, {{"id", id}, {"generation", b["generation"].toInt()}}, e)) {
            *e += QStringLiteral(" 已创建的会话保留供核对：") + session;
            return {};
        }
        auto sessions = b["sessions"].toArray();
        if (!sessions.contains(session))
            sessions.append(session);
        b.insert("session", session);
        b.insert("sessions", sessions);
        b.insert("state", Paused);
        b.insert("revision", b["revision"].toInt() + 1);
        auto next = m_bots;
        next[i] = b;
        if (!commit(next, e))
            return {};
        return b;
    }
    *e = QStringLiteral("Bot 不存在。");
    return {};
}
QJsonObject Service::execute(const QString &a, const QJsonObject &p, QString *e) {
    if (!m_valid) {
        *e = QStringLiteral("Bot 注册表无效，已暂停读取和修改。");
        return {};
    }
    if (a == "list") {
        QJsonArray summaries;
        for (const auto &v : m_bots) {
            auto bot = v.toObject();
            bot.insert("rolePreview", bot.take("role").toString().left(256));
            bot.insert("chatCount", bot.take("sessions").toArray().size());
            summaries.append(bot);
        }
        // 列表不返回职责全文和聊天ID数组，保证最大页也在IPC帧容量内。
        return control::paginate(summaries, p, e);
    }
    if (a == "create") {
        if (!label(p["name"], 64) || !role(p["role"]) ||
            !m_host.chat.projects()->catalog.find(p["project"].toString()) ||
            (!p["workspace"].toString().isEmpty() &&
             !m_host.chat.projects()->catalog.workspace(p["project"].toString(),
                                                        p["workspace"].toString()))) {
            *e = QStringLiteral("Bot 名称、职责、项目或工作区无效。");
            return {};
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject b{{"id", id},
                      {"name", p["name"].toString().trimmed()},
                      {"role", p["role"]},
                      {"project", p["project"]},
                      {"workspace", p["workspace"].toString()},
                      {"generation", 0},
                      {"host", "local"},
                      {"agent", "codex"},
                      {"state", Preparing},
                      {"revision", 0},
                      {"session", ""},
                      {"sessions", QJsonArray{}}};
        auto next = m_bots;
        next.append(b);
        if (!commit(next, e))
            return {};
        auto result = prepare(id, e);
        if (!e->isEmpty())
            *e += QStringLiteral(" Bot 准备记录已保留，可用 bots.prepare 重试，ID：") + id;
        return result;
    }
    if (a == "prepare")
        return prepare(p["id"].toString(), e);
    for (qsizetype i = 0; i < m_bots.size(); ++i) {
        auto b = m_bots[i].toObject();
        if (b["id"] != p["id"])
            continue;
        if (a == "get") {
            b.insert("running", busy(b) ? 1 : 0);
            return b;
        }
        if (a == "open")
            return m_host.invoke("sessions.select", {{"session", b["session"]}}, e);
        if (a == "send") {
            if (b["state"].toInt() != Active) {
                *e = QStringLiteral("Bot 已暂停或尚未就绪。");
                return {};
            }
            return m_host.invoke("sessions.send", {{"session", b["session"]}, {"prompt", p["prompt"]}}, e);
        }
        if (!ipc::integer(p["revision"], 0, 2147483645) || p["revision"] != b["revision"]) {
            *e = QStringLiteral("Bot 版本已变化，请刷新后重试。");
            return {};
        }
        if (a == "set") {
            if (b["state"].toInt() == Preparing || !ipc::integer(p["state"], Active, Paused)) {
                *e = QStringLiteral("Bot 尚未准备完成或状态无效。");
                return {};
            }
            b.insert("state", p["state"]);
        } else if (a == "update") {
            if (!label(p["name"], 64) || !role(p["role"]) || busy(b)) {
                *e = QStringLiteral("名称或职责无效；运行中请先等待任务结束再修改职责。");
                return {};
            }
            b.insert("name", p["name"].toString().trimmed());
            b.insert("role", p["role"]);
        } else if (a == "new-chat") {
            if (busy(b) || b["sessions"].toArray().size() >= 256 || b["state"].toInt() == Preparing ||
                b["revision"].toInt() > 2147483644 || b["generation"].toInt() >= 2147483645) {
                *e = QStringLiteral("Bot 正在运行、尚未就绪或聊天数量已达上限。");
                return {};
            }
            // 先保留准备意图，再创建新聊天；旧聊天始终留在项目历史中。
            b.insert("session", QString{});
            b.insert("state", Preparing);
            b.insert("generation", b["generation"].toInt() + 1);
        } else {
            *e = QStringLiteral("未知 Bot 操作。");
            return {};
        }
        b.insert("revision", b["revision"].toInt() + 1);
        auto next = m_bots;
        next[i] = b;
        if (!commit(next, e))
            return {};
        // 恢复只重新检查现有运行的待投递信箱，不重发历史任务或自动启动新模型请求。
        if (a == "set" && b["state"].toInt() == Active)
            for (const auto &session : b["sessions"].toArray())
                m_host.chat.agent()->reconsiderSessionInput(session.toString());
        return a == "new-chat" ? prepare(b["id"].toString(), e) : b;
    }
    *e = QStringLiteral("Bot 不存在。");
    return {};
}
} // namespace exagent::bots
