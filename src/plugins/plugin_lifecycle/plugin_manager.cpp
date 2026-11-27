#include "plugin_manager.h"
#include "manifest.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>
namespace exagent::plugins {
namespace {
QString hash(const QJsonObject &manifest) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(manifest).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
}
QSet<QString> strings(const QJsonArray &array) {
  QSet<QString> out;
  for (const auto &v : array)
    out.insert(v.toString());
  return out;
}
QJsonObject summary(QJsonObject row) {
  const auto manifest = row.take("manifest").toObject();
  QJsonArray versions;
  for (const auto &v : row.take("releases").toArray())
    versions.append(v.toObject()["version"]);
  row.insert("name", manifest["name"]);
  row.insert("version", manifest["version"]);
  row.insert("capabilities", manifest["capabilities"]);
  row.insert("versions", versions);
  row.insert("authorization",
             row["approved"] == row["hash"] &&
                     strings(row["grants"].toArray()) ==
                         strings(manifest["capabilities"].toArray())
                 ? 1
                 : 0);
  row.remove("approved");
  return row;
}
} // namespace
Manager::Manager(control::Host &host) : QObject(&host), m_host(host) {
  m_file = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/plugins/registry.json";
  QFile file(m_file);
  if (QFileInfo(m_file).isSymLink())
    m_valid = false;
  else if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly))
      m_valid = false;
    else {
      const auto bytes = file.read(4 * 1024 * 1024 + 1);
      const auto root = QJsonDocument::fromJson(bytes).object();
      m_valid = bytes.size() <= 4 * 1024 * 1024 &&
                file.error() == QFileDevice::NoError &&
                root["version"].toDouble(-1) == 1 && root["entries"].isArray();
      if (m_valid)
        m_entries = root["entries"].toArray();
    }
  }
  QSet<QString> ids;
  for (const auto &v : m_entries) {
    const auto row = v.toObject(), manifest = row["manifest"].toObject();
    QString error;
    if (validateManifest(manifest, &error).isEmpty() ||
        row["id"].toString() != "local." + manifest["id"].toString() ||
        ids.contains(row["id"].toString()) ||
        row["hash"].toString() != hash(manifest) ||
        !ipc::integer(row["state"], PendingApproval, Disabled) ||
        !row["releases"].isArray() || !row["grants"].isArray())
      m_valid = false;
    if (row["state"].toInt() == Enabled &&
        (row["approved"] != row["hash"] ||
         strings(row["grants"].toArray()) !=
             strings(manifest["capabilities"].toArray())))
      m_valid = false;
    // 保留版本必须属于同一插件，版本号唯一，当前冻结内容必须可在版本集中追溯。
    QSet<QString> versions;
    bool currentRetained = false;
    for (const auto &value : row["releases"].toArray()) {
      const auto release = value.toObject();
      const auto version = release["version"].toString();
      if (validateManifest(release, &error).isEmpty() ||
          release["id"] != manifest["id"] || versions.contains(version))
        m_valid = false;
      versions.insert(version);
      currentRetained =
          currentRetained || hash(release) == row["hash"].toString();
    }
    if (!currentRetained)
      m_valid = false;
    ids.insert(row["id"].toString());
  }
  const QList<QPair<QString, QSet<QString>>> methods{
      {"list", {"limit", "after"}},
      {"inspect", {"directory"}},
      {"install", {"directory"}},
      {"upgrade", {"directory"}},
      {"get", {"id"}},
      {"approve", {"id", "hash", "grants"}},
      {"enable", {"id", "hash", "enabled"}},
      {"rollback", {"id", "version"}},
      {"invoke", {"id", "hash", "command", "params"}},
      {"panels", {"id"}},
      {"panel", {"id", "panel"}}};
  for (const auto &method : methods)
    host.add("plugins." + method.first, method.second,
             [this, name = method.first](const QJsonObject &p, QString *e) {
               return execute(name, p, e);
             });
  host.chat.setPlugins(this);
}
QVariantMap Manager::operate(const QString &action, const QVariantMap &params) {
  QString error;
  const auto result =
      execute(action, QJsonObject::fromVariantMap(params), &error);
  return error.isEmpty()
             ? QVariantMap{{"ok", true}, {"result", result.toVariantMap()}}
             : QVariantMap{{"ok", false}, {"error", error}};
}
bool Manager::commit(const QJsonArray &entries, QString *error) {
  if (!m_valid || QFileInfo(m_file).isSymLink()) {
    *error = QStringLiteral("插件存储不可用，已有数据保留。");
    return false;
  }
  const auto bytes =
      QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}})
          .toJson(QJsonDocument::Compact);
  if (bytes.size() > 4 * 1024 * 1024) {
    *error = QStringLiteral("插件版本存储超过 4 MiB，请先人工归档。");
    return false;
  }
  if (!QDir().mkpath(QFileInfo(m_file).absolutePath())) {
    *error = QStringLiteral("无法创建插件存储目录。");
    return false;
  }
  QSaveFile file(m_file);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit()) {
    *error = QStringLiteral("插件状态保存失败，原版本保留。");
    return false;
  }
  m_entries = entries;
  emit changed();
  return true;
}
QJsonObject Manager::inspect(const QString &directory, QString *error) const {
  const auto url = QUrl(directory);
  auto manifest =
      readManifest(url.isLocalFile() ? url.toLocalFile() : directory, error);
  if (manifest.isEmpty())
    return {};
  for (const auto &cap : manifest["capabilities"].toArray())
    if (!m_host.hasMethod(cap.toString())) {
      *error = QStringLiteral("宿主不支持此插件能力：") + cap.toString();
      return {};
    }
  return manifest;
}
QJsonObject Manager::execute(const QString &action, const QJsonObject &p,
                             QString *error) {
  if (!m_valid) {
    *error = QStringLiteral("插件存储格式无效，已暂停加载和修改。");
    return {};
  }
  if (action == "list") {
    QJsonArray rows;
    for (const auto &v : m_entries)
      rows.append(summary(v.toObject()));
    return control::paginate(rows, p, error);
  }
  if (action == "inspect")
    return inspect(p["directory"].toString(), error);
  if (action == "install" || action == "upgrade") {
    const auto manifest = inspect(p["directory"].toString(), error);
    if (manifest.isEmpty())
      return {};
    const auto id = "local." + manifest["id"].toString();
    qsizetype index = -1;
    for (qsizetype i = 0; i < m_entries.size(); ++i)
      if (m_entries[i].toObject()["id"] == id)
        index = i;
    if ((action == "install" && index >= 0) ||
        (action == "upgrade" && index < 0)) {
      *error =
          QStringLiteral("安装身份已存在或升级目标不存在，请核对插件列表。");
      return {};
    }
    QJsonObject row;
    QJsonArray releases;
    if (index >= 0) {
      row = m_entries[index].toObject();
      releases = row["releases"].toArray();
      if (QVersionNumber::compare(
              QVersionNumber::fromString(manifest["version"].toString()),
              QVersionNumber::fromString(
                  row["manifest"].toObject()["version"].toString())) <= 0) {
        *error = QStringLiteral("升级版本必须高于当前版本。");
        return {};
      }
    }
    bool retained = false;
    for (const auto &release : releases) {
      if (release.toObject()["version"] != manifest["version"])
        continue;
      if (hash(release.toObject()) != hash(manifest)) {
        *error = QStringLiteral("已保存此版本的其他内容，请使用更高版本号。");
        return {};
      }
      retained = true;
    }
    if (!retained)
      releases.append(manifest);
    // 版本内容冻结在私有注册表；升级撤销旧授权，源目录后续变化不影响已批准版本。
    row.insert("id", id);
    row.insert("manifest", manifest);
    row.insert("hash", hash(manifest));
    row.insert("releases", releases);
    row.insert("state", PendingApproval);
    row.insert("approved", QString{});
    row.insert("grants", QJsonArray{});
    auto next = m_entries;
    if (index < 0)
      next.append(row);
    else
      next[index] = row;
    if (!commit(next, error))
      return {};
    return summary(row);
  }
  for (qsizetype i = 0; i < m_entries.size(); ++i) {
    auto row = m_entries[i].toObject();
    if (row["id"] != p["id"])
      continue;
    const auto manifest = row["manifest"].toObject();
    if (action == "get") {
      auto result = summary(row);
      result.insert("manifest", manifest);
      return result;
    }
    if (action == "approve") {
      if (p["hash"] != row["hash"]) {
        *error = QStringLiteral("插件版本已变化，请重新检查能力清单。");
        return {};
      }
      const auto required = strings(manifest["capabilities"].toArray());
      if (!p["grants"].isArray() ||
          strings(p["grants"].toArray()) != required ||
          p["grants"].toArray().size() != required.size()) {
        *error = QStringLiteral("请明确确认当前版本所声明的全部能力。");
        return {};
      }
      row.insert("approved", row["hash"]);
      row.insert("grants", p["grants"]);
      row.insert("state", Disabled);
    } else if (action == "enable") {
      if (!ipc::integer(p["enabled"], 0, 1)) {
        *error = QStringLiteral("enabled 必须为 0 或 1。");
        return {};
      }
      if (p["enabled"].toInt() == 1 &&
          (row["approved"] != row["hash"] || p["hash"] != row["hash"] ||
           strings(row["grants"].toArray()) !=
               strings(manifest["capabilities"].toArray()))) {
        *error = QStringLiteral("当前版本尚未授权，请先检查能力清单。");
        return {};
      }
      row.insert("state", p["enabled"].toInt() == 1 ? Enabled : Disabled);
    } else if (action == "rollback") {
      QJsonObject release;
      for (const auto &v : row["releases"].toArray())
        if (v.toObject()["version"] == p["version"])
          release = v.toObject();
      if (release.isEmpty()) {
        *error = QStringLiteral("保留的版本不存在。");
        return {};
      }
      row.insert("manifest", release);
      row.insert("hash", hash(release));
      row.insert("approved", QString{});
      row.insert("grants", QJsonArray{});
      row.insert("state", PendingApproval);
    } else if (action == "invoke" || action == "panels" || action == "panel") {
      if (row["state"].toInt() != Enabled || row["approved"] != row["hash"]) {
        *error = QStringLiteral("插件未启用或此版本授权已失效。");
        return {};
      }
      if (action == "panels") {
        QJsonArray panels;
        for (const auto &v : manifest["panels"].toArray()) {
          auto panel = v.toObject();
          panel.remove("text");
          panels.append(panel);
        }
        return {{"items", panels}};
      }
      if (action == "panel") {
        for (const auto &v : manifest["panels"].toArray())
          if (v.toObject()["id"] == p["panel"])
            return v.toObject();
        *error = QStringLiteral("面板不存在。");
        return {};
      }
      if (p["hash"] != row["hash"]) {
        *error = QStringLiteral("插件版本已变化，请重新读取命令。");
        return {};
      }
      if (!p["params"].isUndefined() && !p["params"].isObject()) {
        *error = QStringLiteral("命令参数须为 JSON 对象。");
        return {};
      }
      for (const auto &v : manifest["commands"].toArray()) {
        const auto command = v.toObject();
        if (command["id"] != p["command"])
          continue;
        const auto method = command["method"].toString();
        if (!strings(row["grants"].toArray()).contains(method)) {
          *error = QStringLiteral("插件缺少此命令能力。");
          return {};
        }
        auto params = command["params"].toObject();
        const auto supplied = p["params"].toObject();
        for (auto it = supplied.begin(); it != supplied.end(); ++it)
          params.insert(it.key(), it.value());
        return m_host.invoke(method, params, error);
      }
      *error = QStringLiteral("插件命令不存在。");
      return {};
    } else {
      *error = QStringLiteral("未知插件操作。");
      return {};
    }
    auto next = m_entries;
    next[i] = row;
    if (!commit(next, error))
      return {};
    return summary(row);
  }
  *error = QStringLiteral("插件不存在。");
  return {};
}
} // namespace exagent::plugins
