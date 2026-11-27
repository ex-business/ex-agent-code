#include "audit_store.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
namespace exagent::sharing {
namespace {
constexpr int MaxRecords = 4096, MaxBytes = 4 * 1024 * 1024;
bool privateFile(const QString &path) {
  const QFileInfo info(path);
  if (info.isSymLink())
    return false;
  if (!info.exists())
    return true;
  const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                       QFileDevice::ExeGroup | QFileDevice::ReadOther |
                       QFileDevice::WriteOther | QFileDevice::ExeOther;
  if (!info.isFile() || (info.permissions() & exposed))
    return false;
#ifdef Q_OS_UNIX
  if (info.ownerId() != uint(getuid()))
    return false;
#endif
  return true;
}
bool integer(const QJsonValue &v, int low, int high) {
  return v.isDouble() && v.toDouble() >= low && v.toDouble() <= high &&
         v.toDouble() == v.toInt(-1);
}
} // namespace
AuditStore::AuditStore() {
  m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/sharing/audit.json";
  QFile file(m_file);
  m_valid = privateFile(m_file) &&
            !QFileInfo(QFileInfo(m_file).absolutePath()).isSymLink();
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly))
      m_valid = false;
    else {
      const auto data = file.read(MaxBytes + 1);
      const auto root = QJsonDocument::fromJson(data).object();
      m_valid = m_valid && data.size() <= MaxBytes &&
                file.error() == QFileDevice::NoError &&
                root["version"].toDouble(-1) == 1 && root.size() == 2 &&
                root["items"].isArray();
      if (m_valid)
        m_items = root["items"].toArray();
    }
  }
  QSet<QString> ids;
  if (m_items.size() > MaxRecords)
    m_valid = false;
  for (qsizetype i = 0; i < m_items.size(); ++i) {
    const auto r = m_items[i].toObject();
    const QSet<QString> fields{"seq",    "id",    "session",   "event", "actor",
                               "object", "phase", "operation", "time"};
    const auto keys = r.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != fields ||
        r["session"].toString().size() > 256 ||
        r["actor"].toString().size() > 256 ||
        r["object"].toString().size() > 256 ||
        r["time"].toString().size() > 40 ||
        !r["time"].toString().endsWith('Z') ||
        !QDateTime::fromString(r["time"].toString(), Qt::ISODateWithMs)
             .isValid() ||
        r["seq"].toDouble() != i + 1 || QUuid(r["id"].toString()).isNull() ||
        ids.contains(r["id"].toString()) ||
        QUuid(r["operation"].toString()).isNull() ||
        r["session"].toString().isEmpty() || !integer(r["event"], 1, 10) ||
        !integer(r["phase"], 1, 3) || !r["actor"].isString() ||
        !r["object"].isString() || !r["time"].isString())
      m_valid = false;
    ids.insert(r["id"].toString());
  }
  if (!m_valid)
    m_state = State::Unavailable;
  else if (m_items.size() >= MaxRecords)
    m_state = State::Full;
}
bool AuditStore::append(const QString &session, Event event,
                        const QString &actor, const QString &object,
                        Phase phase, const QString &operation, QString *error) {
  if (!m_valid || !privateFile(m_file) ||
      QFileInfo(QFileInfo(m_file).absolutePath()).isSymLink()) {
    m_state = State::Unavailable;
    *error = QStringLiteral("共享审计存储不可用，已禁止新增授权。");
    return false;
  }
  if (m_items.size() >= MaxRecords) {
    m_state = State::Full;
    *error = QStringLiteral("共享审计已达4096条保留上限，原记录保留。");
    return false;
  }
  if (int(event) < 1 || int(event) > 10 || int(phase) < 1 || int(phase) > 3 ||
      session.isEmpty() || session.size() > 256 || actor.size() > 256 ||
      object.size() > 256 || QUuid(operation).isNull()) {
    *error = QStringLiteral("共享审计对象无效。");
    return false;
  }
  auto next = m_items;
  next.append(QJsonObject{
      {"seq", int(m_items.size() + 1)},
      {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"session", session},
      {"event", int(event)},
      {"actor", actor},
      {"object", object},
      {"phase", int(phase)},
      {"operation", operation},
      {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"items", next}})
                         .toJson(QJsonDocument::Compact);
  if (bytes.size() > MaxBytes) {
    m_state = State::Full;
    *error = QStringLiteral("共享审计已达容量上限，原记录保留。");
    return false;
  }
  QSaveFile f(m_file);
  f.setDirectWriteFallback(false);
  if (!QDir().mkpath(QFileInfo(m_file).absolutePath()) ||
      !f.open(QIODevice::WriteOnly) ||
      !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      f.write(bytes) != bytes.size() || !f.commit()) {
    m_state = State::Unavailable;
    *error = QStringLiteral("共享审计写入失败，原记录保留。");
    return false;
  }
  m_items = next;
  m_state = m_items.size() >= MaxRecords ? State::Full : State::Ready;
  return true;
}
QJsonObject AuditStore::page(const QString &session, int after, int limit,
                             QString *error) const {
  if (!m_valid) {
    *error = QStringLiteral("共享审计文件无效，无法读取。");
    return {};
  }
  if (after < 0 || after > m_items.size() || limit < 1 || limit > 100) {
    *error = QStringLiteral("共享审计分页参数无效。");
    return {};
  }
  QJsonArray rows;
  int cursor = after, bytes = 0;
  // 页大小同时受条数和字节限制，异常大元数据不能拖垮本机RPC传输。
  for (int i = after; i < m_items.size(); ++i) {
    const auto item = m_items[i].toObject();
    if (item["session"] == session) {
      const auto size =
          QJsonDocument(item).toJson(QJsonDocument::Compact).size() + 1;
      if (bytes + size > 192 * 1024)
        break;
      rows.append(item);
      bytes += size;
    }
    cursor = i + 1;
    if (rows.size() == limit)
      break;
  }
  return {{"items", rows},
          {"next", cursor},
          {"end", int(m_items.size())},
          {"auditState", int(m_state)}};
}
} // namespace exagent::sharing
