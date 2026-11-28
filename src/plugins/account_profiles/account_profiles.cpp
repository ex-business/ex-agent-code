#include "account_profiles.h"
#include "agent/openai/credentials.h"
#include "agent/runtime_run.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
namespace exagent::accounts {
QString reference(const QVariantMap &config) {
  const auto explicitId = config.value("accountProfile").toString();
  if (!explicitId.isEmpty())
    return explicitId;
  if (config.value("account").toInt() == 3)
    return "legacy-api-" +
           QString::fromLatin1(
               QCryptographicHash::hash(
                   config.value("credentialFile").toString().toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex()
                   .left(16));
  return config.value("account").toInt() == 2 ? "dedicated" : "system";
}
namespace {
bool writeJson(const QString &path, const QJsonObject &object) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return f.write(bytes) == bytes.size() && f.commit();
}
QJsonObject load(const QString &path, qint64 limit) {
  QFile f(path);
  if (!f.exists())
    return {};
  if (QFileInfo(path).isSymLink() || f.size() > limit ||
      !f.open(QIODevice::ReadOnly))
    return {{"_invalid", true}};
  QJsonParseError e;
  const auto d = QJsonDocument::fromJson(f.readAll(), &e);
  return e.error == QJsonParseError::NoError && d.isObject()
             ? d.object()
             : QJsonObject{{"_invalid", true}};
}
bool sameCredential(const QJsonObject &a, const QJsonObject &b) {
  const auto x = a["credentialFile"].toString(),
             y = b["credentialFile"].toString();
  if (x.isEmpty() || y.isEmpty())
    return x == y;
  const auto cx = QFileInfo(x).canonicalFilePath(),
             cy = QFileInfo(y).canonicalFilePath();
  if (!cx.isEmpty() && cx == cy)
    return true;
#ifdef Q_OS_UNIX
  struct stat sx {
  }, sy{};
  return ::stat(QFile::encodeName(x).constData(), &sx) == 0 &&
         ::stat(QFile::encodeName(y).constData(), &sy) == 0 &&
         sx.st_dev == sy.st_dev && sx.st_ino == sy.st_ino;
#else
  return false;
#endif
}
bool validUsage(const QJsonObject &root) {
  if (root.isEmpty())
    return true;
  if (root.contains("coverage") && root["coverage"] != 1 && root["coverage"] != 2)
    return false;
  if (root["version"] != 1 || !root["runs"].isObject() ||
      root["runs"].toObject().size() > 10000)
    return false;
  const auto runs = root["runs"].toObject();
  for (auto it = runs.begin(); it != runs.end(); ++it) {
    const auto row = it.value().toObject();
    const auto usage = row["usage"].toObject();
    if (QUuid(it.key()).isNull() || row["profile"].toString().isEmpty() ||
        row["profile"].toString().size() > 80 || usage.isEmpty())
      return false;
    for (auto u = usage.begin(); u != usage.end(); ++u)
      if ((u.key() != "input_tokens" && u.key() != "output_tokens" &&
           u.key() != "total_tokens") ||
          !u.value().isDouble() || u.value().toInteger(-1) < 0 ||
          u.value().toInteger() > 1000000000000LL ||
          double(u.value().toInteger()) != u.value().toDouble())
        return false;
  }
  return true;
}
int state(const QJsonObject &p) {
  const auto s = p["status"].toInt(Unverified);
  if (s == Limited && p["resetAt"].toInteger() > 0 &&
      p["resetAt"].toInteger() <= QDateTime::currentSecsSinceEpoch())
    return Ready;
  return s;
}
} // namespace
Profiles::Profiles(QObject *parent) : QObject(parent) {
  m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
          "/account-profiles";
  const bool dir = !QFileInfo(m_dir).isSymLink() && QDir().mkpath(m_dir) &&
                   QFile::setPermissions(m_dir, QFileDevice::ReadOwner |
                                                    QFileDevice::WriteOwner |
                                                    QFileDevice::ExeOwner);
  m_lock = std::make_unique<QLockFile>(m_dir + "/catalog.lock");
  // 此锁覆盖整个实例生命周期，不能按短操作的默认超时判定为过期。
  m_lock->setStaleLockTime(std::chrono::milliseconds::zero());
  m_writable = dir && m_lock->tryLock(0);
  auto root = load(m_dir + "/catalog.json", 1048576);
  if (!root.isEmpty() &&
      (root["version"] != 1 || !root["profiles"].isObject() ||
       root["profiles"].toObject().size() > 64)) {
    m_writable = false;
    m_error = QStringLiteral("账号目录损坏，已停止写入，请保留原文件后检查。");
  } else {
    const auto entries = root["profiles"].toObject();
    bool valid = true;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      const auto p = it.value().toObject();
      const auto id = it.key();
      const auto kind = p["kind"].toInt(-1), status = p["status"].toInt(-1),
                 enabled = p["enabled"].toInt(-1);
      const bool builtin = p["builtin"].isBool() && p["builtin"].toBool();
      const bool uuid =
          !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
      const bool legacy =
          QRegularExpression("^legacy-api-[0-9a-f]{16}$").match(id).hasMatch();
      const bool idOk = (id == "system" && kind == 1 && builtin) ||
                        (id == "dedicated" && kind == 2 && builtin) ||
                        (legacy && kind == 3 && builtin) ||
                        (uuid && (kind == 1 || kind == 3) &&
                         p["builtin"].isBool() && !builtin);
      if (!idOk || p["id"] != id || p["name"].toString().isEmpty() ||
          p["name"].toString().size() > 80 ||
          (kind != 1 && kind != 2 && kind != 3) || status < 0 || status > 4 ||
          (enabled != 1 && enabled != 2) ||
          p["credentialFile"].toString().size() > 4096 ||
          p["model"].toString().size() > 128 ||
          p["scope"].toString().size() > 200) {
        valid = false;
        break;
      }
    }
    if (valid)
      m_profiles = entries;
    else {
      m_writable = false;
      m_error = QStringLiteral("账号元数据契约无效，已停止覆盖原文件。");
    }
  }
  for (const auto &entry :
       QList<QJsonObject>{{{"id", "system"},
                           {"name", QStringLiteral("本机 Codex")},
                           {"kind", 1}},
                          {{"id", "dedicated"},
                           {"name", QStringLiteral("EX 专用 Codex")},
                           {"kind", 2}}}) {
    const auto id = entry["id"].toString();
    if (!m_profiles.contains(id)) {
      auto p = entry;
      p.insert("status", Unverified);
      p.insert("enabled", 1);
      p.insert("builtin", true);
      m_profiles.insert(id, p);
    }
  }
  m_flush.setSingleShot(true);
  m_flush.setInterval(250);
  connect(&m_flush, &QTimer::timeout, this, &Profiles::writeUsage);
  connect(&m_writer, &QFutureWatcher<bool>::finished, this, [this] {
    if (!m_writer.result()) {
      m_dirty = true;
      m_error = QStringLiteral("账号用量保存失败，内存记录保留。");
      emit failure(m_error);
      emit changed();
      return;
    }
    if (m_dirty)
      m_flush.start();
  });
  connect(&m_reader, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    auto root = m_reader.result();
    if (!validUsage(root)) {
      m_healthy = false;
      m_error = QStringLiteral("用量记录损坏，已停止覆盖原文件。");
      emit failure(m_error);
      emit changed();
      return;
    }
    auto loaded = root["runs"].toObject();
    for (auto it = m_records.begin(); it != m_records.end(); ++it)
      loaded.insert(it.key(), it.value());
    // 首轮派发等待读取完成，不接受载入期间的新用量，避免突破同一容量边界。
    m_records = loaded;
    m_incomplete = root["coverage"].toInt(1) == 2;
    m_totals.clear();
    for (const auto &v : std::as_const(m_records))
      addTotal(v.toObject(), 1);
    m_loaded = true;
    if (m_dirty)
      m_flush.start();
    emit changed();
  });
  const auto path = m_dir + "/usage.json";
  m_reader.setFuture(
      QtConcurrent::run([path] { return load(path, 16777216); }));
}
Profiles::~Profiles() {
  m_flush.stop();
  m_reader.disconnect(this);
  m_writer.disconnect(this);
  m_reader.waitForFinished();
  m_writer.waitForFinished();
  const auto read = m_reader.result();
  const bool valid = validUsage(read);
  // 不能依赖尚未投递的finished槽：同步检查读写结果，损坏原文件一律不覆盖。
  const bool failedWriter = m_writer.future().isValid() && !m_writer.result();
  if (writable() && m_healthy && valid && m_loaded && (m_dirty || failedWriter))
    writeJson(m_dir + "/usage.json", {{"version", 1},
                                      {"coverage", m_incomplete ? 2 : 1},
                                      {"runs", m_records}});
}
bool Profiles::save() {
  if (!writable()) {
    m_error = QStringLiteral("账号目录只读或由其他实例占用。");
    emit failure(m_error);
    return false;
  }
  if (QJsonDocument(QJsonObject{{"version", 1}, {"profiles", m_profiles}})
          .toJson(QJsonDocument::Compact)
          .size() > 1048576) {
    m_error = QStringLiteral("账号元数据超过容量限制，未覆盖原文件。");
    emit failure(m_error);
    return false;
  }
  if (writeJson(m_dir + "/catalog.json",
                {{"version", 1}, {"profiles", m_profiles}}))
    return true;
  m_error = QStringLiteral("账号目录保存失败。");
  emit failure(m_error);
  return false;
}
bool Profiles::put(QJsonObject p) {
  auto before = m_profiles;
  m_profiles.insert(p["id"].toString(), p);
  if (!save()) {
    m_profiles = before;
    emit changed();
    return false;
  }
  emit changed();
  return true;
}
QJsonObject Profiles::profile(const QString &id) const {
  return m_profiles[id].toObject();
}
QString Profiles::home(const QString &id) const {
  const auto p = profile(id);
  return p["kind"] == 1 && !p["builtin"].toBool() ? m_dir + "/" + id + "/codex"
                                                  : QString{};
}
QString Profiles::create(int kind, const QString &name, const QString &file,
                         const QString &model, const QString &scope) {
  if (!writable() || m_profiles.size() >= 64 || name.trimmed().isEmpty() ||
      name.size() > 80 || (kind != 1 && kind != 3) || model.size() > 128 ||
      scope.size() > 200) {
    m_error = QStringLiteral("账号类型、名称或数量无效。");
    emit failure(m_error);
    return {};
  }
  if (kind == 3) {
    QString e;
    auto key = openai::credential(file, &e);
    const bool ok = !key.isEmpty();
    key.fill(0);
    if (!ok) {
      m_error = e;
      emit failure(e);
      return {};
    }
  }
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QJsonObject p{{"id", id},
                {"name", name.trimmed()},
                {"kind", kind},
                {"credentialFile", kind == 3 ? file : QString{}},
                {"model", model.trimmed()},
                {"scope", scope},
                {"enabled", 1},
                {"status", Unverified},
                {"builtin", false}};
  return put(p) ? id : QString{};
}
bool Profiles::legacyApi(const QString &file, const QString &model) {
  auto p = profile(reference({{"account", 3}, {"credentialFile", file}}));
  if (p.isEmpty() && m_profiles.size() >= 64) {
    m_error =
        QStringLiteral("账号目录已达64条，旧会话保持原样，未新增凭据引用。");
    emit failure(m_error);
    return false;
  }
  p.insert("id", reference({{"account", 3}, {"credentialFile", file}}));
  p.insert("kind", 3);
  p.insert("name", QStringLiteral("默认 OpenAI API"));
  p.insert("builtin", true);
  // 重启迁移只补登记，不能重新启用用户已停用的凭据。
  if (!p.contains("enabled"))
    p.insert("enabled", 1);
  p.insert("model", model);
  if (p["credentialFile"] != file)
    p.insert("status", Unverified);
  p.insert("credentialFile", file);
  return put(p);
}
bool Profiles::resetAvailability(const QString &id) {
  auto p = profile(id);
  if (p.isEmpty())
    return false;
  p.insert("status", Unverified);
  p.insert("resetAt", 0);
  return put(p);
}
bool Profiles::setEnabled(const QString &id, bool enabled) {
  auto p = profile(id);
  if (p.isEmpty())
    return false;
  p.insert("enabled", enabled ? 1 : 2);
  return put(p);
}
QJsonObject Profiles::route(const QString &id, const QString &model,
                            bool automatic) const {
  const auto selected = profile(id);
  if (selected.isEmpty() || selected["enabled"] != 1)
    return {};
  const auto s = state(selected);
  if (s != Limited && s != NeedsLogin)
    return selected;
  // 只在新派发前从已验证、同引擎且模型相容的账号选择；不重试原提示，也不跨工具能力换引擎。
  if (automatic && s == Limited)
    for (const auto &v : m_profiles) {
      const auto p = v.toObject();
      const auto a = selected["kind"].toInt(), b = p["kind"].toInt();
      if (p["id"] == id || p["enabled"] != 1 || state(p) != Ready ||
          (a == 3) != (b == 3) ||
          (!p["model"].toString().isEmpty() && p["model"] != model))
        continue;
      if (a == 3 && sameCredential(selected, p))
        continue;
      if (a != 3 && !selected["owner"].toString().isEmpty() &&
          selected["owner"] == p["owner"])
        continue;
      if (!selected["scope"].toString().isEmpty() &&
          selected["scope"] == p["scope"])
        continue;
      return p;
    }
  return {};
}
void Profiles::observe(const QString &id, const QJsonObject &e) {
  auto p = profile(id);
  if (p.isEmpty() || !writable())
    return;
  if (e["kind"] == "accountInfo") {
    if (state(p) != Limited && state(p) != NeedsLogin)
      p.insert("status", Ready);
    if (e["owner"].isString())
      p.insert("owner", e["owner"].toString().left(200));
    if (e["plan"].isString() && !e["plan"].toString().isEmpty())
      p.insert("plan", e["plan"].toString().left(100));
  }
  if (e["kind"] == "accountLimits") {
    const auto limits = e["limits"].toObject();
    if (QJsonDocument(limits).toJson().size() > 8192)
      return;
    p.insert("limits", limits);
    if (limits["planType"].isString())
      p.insert("plan", limits["planType"]);
    bool limited = false, observed = false;
    qint64 reset = 0;
    for (const auto &key : {"primary", "secondary"}) {
      const auto w = limits[key].toObject();
      if (!w["usedPercent"].isDouble() || w["usedPercent"].toDouble() < 0 ||
          w["usedPercent"].toDouble() > 100)
        continue;
      observed = true;
      if (w["usedPercent"].toDouble(-1) >= 100) {
        limited = true;
        const auto t = w["resetsAt"].toInteger();
        if (t <= 0)
          reset = -1;
        else if (reset != -1)
          reset = qMax(reset, t);
      }
    }
    if (observed) {
      p.insert("status", limited ? Limited : Ready);
      p.insert("resetAt", reset);
    }
  }
  if (e["kind"] == "accountUnavailable") {
    const auto s = e["status"].toInt();
    if (s != Limited && s != NeedsLogin)
      return;
    p.insert("status", s);
    p.insert("resetAt", e["resetAt"].toInteger());
  }
  p.insert("observedAt",
           QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  put(p);
}
void Profiles::addTotal(const QJsonObject &r, int d) {
  auto &t = m_totals[r["profile"].toString()];
  auto u = r["usage"].toObject();
  t.runs += d;
  if (u.contains("input_tokens")) {
    t.input += d * u["input_tokens"].toInteger();
    t.inputs += d;
  }
  if (u.contains("output_tokens")) {
    t.output += d * u["output_tokens"].toInteger();
    t.outputs += d;
  }
  if (u.contains("total_tokens")) {
    t.tokens += d * u["total_tokens"].toInteger();
    t.totals += d;
  }
}
void Profiles::usage(const QString &id, const QString &run,
                     const QJsonObject &u) {
  if (!writable() || !m_loaded || !m_healthy || profile(id).isEmpty() ||
      QUuid(run).isNull())
    return;
  if (!m_records.contains(run) && m_records.size() >= 10000) {
    m_incomplete = true;
    m_dirty = true;
    m_flush.start();
    const auto reason = QStringLiteral(
        "用量已达10000条统计容量，汇总不完整；原记录和聊天快照保留。");
    if (m_error != reason) {
      m_error = reason;
      emit failure(reason);
      emit changed();
    }
    return;
  }
  QJsonObject clean;
  for (const auto &k : {"input_tokens", "output_tokens", "total_tokens"})
    if (u.contains(k)) {
      const auto n = u[k].toInteger(-1);
      if (n < 0 || n > 1000000000000LL)
        return;
      clean.insert(k, n);
    }
  if (clean.isEmpty())
    return;
  // 累计快照按runId替换，不因重放通知或多轮工具调用重复计费统计。
  const auto before = m_records[run].toObject();
  if (!before.isEmpty() && before["profile"] == id && before["usage"] == clean)
    return;
  if (!before.isEmpty() && before["profile"] != id)
    return;
  if (!before.isEmpty())
    addTotal(before, -1);
  QJsonObject row{{"profile", id},
                  {"usage", clean},
                  {"updatedAt", QDateTime::currentDateTimeUtc().toString(
                                    Qt::ISODateWithMs)}};
  m_records.insert(run, row);
  addTotal(row, 1);
  m_dirty = true;
  if (m_loaded)
    m_flush.start();
  emit changed();
}
void Profiles::writeUsage() {
  if (!writable() || !m_loaded || !m_healthy || !m_dirty ||
      m_writer.isRunning())
    return;
  m_dirty = false;
  const auto rows = m_records;
  const int coverage = m_incomplete ? 2 : 1;
  const auto path = m_dir + "/usage.json";
  m_writer.setFuture(QtConcurrent::run([rows, path, coverage] {
    return writeJson(path,
                     {{"version", 1}, {"coverage", coverage}, {"runs", rows}});
  }));
}
QVariantList Profiles::rows() const {
  QVariantList rows;
  for (const auto &v : m_profiles) {
    auto p = v.toObject();
    const auto id = p["id"].toString();
    const auto t = m_totals.value(id);
    p.insert("status", p["enabled"] == 2 ? Disabled : state(p));
    p.insert("knownRuns", t.runs);
    p.insert("input",
             t.inputs ? QString::number(t.input) : QStringLiteral("—"));
    p.insert("output",
             t.outputs ? QString::number(t.output) : QStringLiteral("—"));
    p.insert("tokens",
             t.totals ? QString::number(t.tokens) : QStringLiteral("—"));
    p.insert("home", home(id));
    rows.append(p.toVariantMap());
  }
  return rows;
}
} // namespace exagent::accounts
