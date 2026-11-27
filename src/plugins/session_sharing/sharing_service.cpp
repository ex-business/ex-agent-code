#include "sharing_service.h"
#include "plugins/mobile_gateway/mobile_service.h"
#include <QDateTime>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QTimer>
#include <QUuid>
Q_LOGGING_CATEGORY(sharingLog, "exagent.sharing")
namespace exagent::sharing {
Service::Service(control::Host &host)
    : QObject(&host), m_host(host),
      m_epoch(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
  if (auto *gateway = qobject_cast<mobile::Service *>(host.chat.mobile())) {
    QPointer<Service> guard(this);
    gateway->auditShared = [guard](const QString &session, int event,
                                   const QString &actor, const QString &object,
                                   int phase, const QString &op, QString *e) {
      if (!guard) {
        *e = QStringLiteral("共享审计插件已退出。");
        return false;
      }
      return guard->m_audit.append(session, Event(event), actor, object,
                                   Phase(phase), op, e);
    };
  }
  m_clock.start();
  auto *expiry = new QTimer(this);
  expiry->setInterval(1000);
  connect(expiry, &QTimer::timeout, this, [this] {
    for (const auto &id : m_leases.keys())
      lease(id);
  });
  expiry->start();
  const QList<QPair<QString, QSet<QString>>> methods{
      {"offer", {"session", "scope"}},
      {"status", {"session"}},
      {"invites", {"session"}},
      {"cancel-invite", {"session", "id"}},
      {"close", {"session", "epoch", "revision"}},
      {"revoke", {"session", "device"}},
      {"audit", {"session", "after", "limit"}},
      {"grant", {"session", "device", "epoch", "revision"}},
      {"reclaim", {"session", "epoch", "revision"}},
      {"access", {"session", "device"}},
      {"request", {"session", "device", "action", "epoch", "revision"}},
      {"send", {"session", "device", "prompt"}}};
  for (const auto &method : methods)
    host.add("sharing." + method.first, method.second,
             [this, a = method.first](const QJsonObject &p, QString *e) {
               return execute(a, p, e);
             });
  // 全局守卫仅约束已有有效共享租约的会话；不改变未共享会话和原有插件策略。
  host.chat.agent()->registerInputPolicy(
      "session-sharing", this,
      [this](const ChatSession &s, int channel, QString &, QString *e) {
        auto &current = lease(s.id);
        if (current.device.isEmpty())
          return true;
        if (channel == AgentController::ChatInput &&
            m_dispatchSession == s.id && m_dispatchDevice == current.device)
          return true;
        *e = QStringLiteral("此会话控制权已交给协作者，请先在共享面板收回。");
        return false;
      },
      true);
}
QJsonObject Service::device(const QString &id) const {
  QString e;
  for (const auto &v :
       m_host.invoke("mobile.devices", {}, &e)["items"].toArray())
    if (v.toObject()["id"] == id)
      return v.toObject();
  return {};
}
bool Service::eligible(const QJsonObject &d, const QString &session) const {
  return d["state"].toInt() == 1 && d["sharing"].toInt() == 1 &&
         d["scope"].toInt() == 2 && d["session"] == session &&
         d["expires"].toDouble() > QDateTime::currentSecsSinceEpoch();
}
Service::Lease &Service::lease(const QString &session) {
  auto &value = m_leases[session];
  // 使用单调时钟判断失联；撤销、授权过期也在每次派发前重新核实。
  if (!value.device.isEmpty() && (value.deadline <= m_clock.elapsed() ||
                                  !eligible(device(value.device), session))) {
    const auto previous = value.device;
    const auto op = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString warning;
    if (!m_audit.append(session, Event::Expire, "runtime", previous,
                        Phase::Completed, op, &warning))
      qCWarning(sharingLog) << warning;
    value.device.clear();
    value.deadline = 0;
    ++value.revision;
    qCInfo(sharingLog) << "共享租约已释放，会话：" << session << "版本："
                       << value.revision;
    QTimer::singleShot(0, this, [this, session] {
      m_host.chat.agent()->reconsiderSessionInput(session);
    });
  }
  return value;
}
QJsonObject Service::view(const QString &session) {
  const auto &value = lease(session);
  QJsonArray requests;
  for (const auto &id : value.requests)
    requests.append(id);
  return {{"epoch", m_epoch},
          {"revision", value.revision},
          {"owner", value.device},
          {"state", value.device.isEmpty() ? 0 : 1},
          {"remainingMs", qMax(qint64(0), value.deadline - m_clock.elapsed())},
          {"requests", requests}};
}
QJsonObject Service::execute(const QString &a, const QJsonObject &p,
                             QString *e) {
  const auto session = p["session"].toString();
  if (session.isEmpty() ||
      m_host.invoke("sessions.show", {{"session", session}}, e).isEmpty()) {
    if (e->isEmpty())
      *e = QStringLiteral("共享会话不存在。");
    return {};
  }
  if (a == "offer")
    return m_host.invoke(
        "mobile.offer",
        {{"session", session}, {"scope", p["scope"]}, {"sharing", 1}}, e);
  if (a == "status") {
    auto result = view(session);
    QJsonArray rows;
    for (const auto &v :
         m_host.invoke("mobile.devices", {}, e)["items"].toArray()) {
      const auto d = v.toObject();
      if (d["session"] == session && d["sharing"].toInt() == 1)
        rows.append(d);
    }
    result.insert("devices", rows);
    result.insert("auditState", m_audit.state());
    return result;
  }
  if (a == "invites")
    return m_host.invoke("mobile.offers", {{"session", session}}, e);
  if (a == "cancel-invite")
    return m_host.invoke("mobile.cancel-offer",
                         {{"session", session}, {"id", p["id"]}}, e);
  if (a == "audit") {
    const auto after = p["after"].isUndefined() ? QJsonValue(0) : p["after"],
               limit = p["limit"].isUndefined() ? QJsonValue(20) : p["limit"];
    if (!ipc::integer(after, 0, 4096) || !ipc::integer(limit, 1, 100)) {
      *e = QStringLiteral("审计分页参数无效。");
      return {};
    }
    return m_audit.page(session, after.toInt(), limit.toInt(), e);
  }
  const auto id = p["device"].toString();
  const auto d = device(id);
  auto &current = lease(session);
  if (a == "revoke") {
    if (d["session"] != session || d["sharing"].toInt() != 1) {
      *e = QStringLiteral("设备不属于此共享会话。");
      return {};
    }
    auto result = m_host.invoke("mobile.revoke", {{"id", id}}, e);
    lease(session);
    return result;
  }
  if (a == "access") {
    if (current.device == id)
      current.deadline = m_clock.elapsed() + 60000;
    auto result = view(session);
    result.insert("canSend",
                  current.device == id && eligible(d, session) ? 1 : 0);
    // 远端不接收其他设备名称和请求列表，只知道自己是否持有控制权。
    result.remove("requests");
    result.insert("owner", current.device == id ? id : QString());
    return result;
  }
  const auto checkRevision = [&] {
    if (p["epoch"] != m_epoch || !ipc::integer(p["revision"], 0, 2147483646) ||
        p["revision"].toInt() != current.revision) {
      *e = QStringLiteral("控制权版本已变化，请刷新后再操作。");
      return false;
    }
    return true;
  };
  if (a == "close") {
    if (!checkRevision())
      return {};
    auto result =
        m_host.invoke("mobile.close-shared", {{"session", session}}, e);
    if (!e->isEmpty())
      return {};
    // 关闭只撤销此会话的共享授权；普通移动设备与其他会话继续沿用既有授权。
    current.device.clear();
    current.deadline = 0;
    current.requests.clear();
    ++current.revision;
    QTimer::singleShot(0, this, [this, session] {
      m_host.chat.agent()->reconsiderSessionInput(session);
    });
    result.insert("auditState", m_audit.state());
    return result;
  }
  if (a == "grant" || a == "reclaim") {
    if (!checkRevision())
      return {};
    if (a == "grant" && !m_host.chat.agent()->canTransferInput(session)) {
      *e = QStringLiteral("线程正在外部入口使用，请先退出该终端再交接。");
      return {};
    }
    if (a == "grant" && !eligible(d, session)) {
      *e = QStringLiteral("目标协作者没有有效的发送授权。");
      return {};
    }
    const auto op = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto event = a == "grant" ? Event::Grant : Event::Reclaim;
    QString warning;
    const auto actor = QStringLiteral("desktop"),
               object = a == "grant" ? id : current.device;
    // 新增控制权必须先留下意图；撤回操作不因审计故障而被阻止。
    const bool recorded = m_audit.append(session, event, actor, object,
                                         Phase::Intent, op, &warning);
    if (!recorded && a == "grant") {
      *e = warning;
      return {};
    }
    if (!recorded)
      qCWarning(sharingLog) << warning;
    // 交接只改变后续输入入口；不取消或重新启动当前正在执行的 Agent。
    current.device = a == "grant" ? id : QString();
    current.deadline = current.device.isEmpty() ? 0 : m_clock.elapsed() + 60000;
    current.requests.remove(id);
    ++current.revision;
    if (!m_audit.append(session, event, actor, object, Phase::Completed, op,
                        &warning))
      qCWarning(sharingLog) << warning;
    qCInfo(sharingLog) << "共享控制权已更新，会话：" << session << "持有设备："
                       << current.device << "版本：" << current.revision;
    if (current.device.isEmpty())
      QTimer::singleShot(0, this, [this, session] {
        m_host.chat.agent()->reconsiderSessionInput(session);
      });
    return view(session);
  }
  if (!eligible(d, session)) {
    *e = QStringLiteral("此协作者没有有效的发送授权。");
    return {};
  }
  if (a == "request") {
    if (!ipc::integer(p["action"], 1, 2)) {
      *e = QStringLiteral("控制请求无效。");
      return {};
    }
    if (p["action"].toInt() == 1) {
      if (current.requests.contains(id))
        return view(session);
      const auto op = QUuid::createUuid().toString(QUuid::WithoutBraces);
      QString warning;
      if (!m_audit.append(session, Event::Request, id, id, Phase::Completed, op,
                          &warning)) {
        *e = warning;
        return {};
      }
      current.requests.insert(id);
      return view(session);
    }
    if (!checkRevision() || current.device != id) {
      if (e->isEmpty())
        *e = QStringLiteral("只有当前持有者可以交还。");
      return {};
    }
    const auto op = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString warning;
    if (!m_audit.append(session, Event::Release, id, id, Phase::Completed, op,
                        &warning))
      qCWarning(sharingLog) << warning;
    current.device.clear();
    current.deadline = 0;
    ++current.revision;
    qCInfo(sharingLog) << "协作者已交还控制权，会话：" << session << "版本："
                       << current.revision;
    QTimer::singleShot(0, this, [this, session] {
      m_host.chat.agent()->reconsiderSessionInput(session);
    });
    return view(session);
  }
  if (a == "send") {
    if (current.device != id) {
      *e = QStringLiteral("尚未取得控制权，请向桌面申请交接。");
      return {};
    }
    QScopedValueRollback<QString> sessionGuard(m_dispatchSession, session),
        deviceGuard(m_dispatchDevice, id);
    return m_host.invoke("sessions.send",
                         {{"session", session}, {"prompt", p["prompt"]}}, e);
  }
  *e = QStringLiteral("未知共享操作。");
  return {};
}
} // namespace exagent::sharing
