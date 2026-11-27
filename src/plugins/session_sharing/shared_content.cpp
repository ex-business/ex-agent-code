#include "shared_content.h"
#include "infrastructure/storage/conversation_image.h"
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrentRun>
namespace exagent::sharing {
namespace {
bool number(const QJsonValue &v, qint64 maximum) {
  return v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= maximum &&
         v.toDouble() == v.toInteger(-1);
}
bool fields(const QJsonObject &p, const QSet<QString> &allowed) {
  for (auto it = p.begin(); it != p.end(); ++it)
    if (!allowed.contains(it.key()))
      return false;
  return true;
}
} // namespace
QString contentRevision(const ChatMessage &message) {
  struct Revision {
    QString content, hash;
    QVariantList attachments;
  };
  // Qt隐式共享使未变化内容的比较与复用不复制正文；容量按保留文本字节计费。
  thread_local QCache<QString, Revision> cache(64 * 1024 * 1024);
  if (const auto *r = cache.object(message.id);
      r && r->content == message.content &&
      r->attachments == message.attachments)
    return r->hash;
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(message.content.toUtf8());
  hash.addData(QJsonDocument(QJsonArray::fromVariantList(message.attachments))
                   .toJson(QJsonDocument::Compact));
  const auto result = QString::fromLatin1(hash.result().toHex());
  const auto cost =
      qMax(qsizetype(65536),
           message.content.size() * 2 +
               QJsonDocument(QJsonArray::fromVariantList(message.attachments))
                   .toJson(QJsonDocument::Compact)
                   .size() +
               4096);
  if (cost <= cache.maxCost())
    cache.insert(message.id,
                 new Revision{message.content, result, message.attachments},
                 int(cost));
  return result;
}
QJsonArray attachmentSummary(const ChatMessage &message) {
  QJsonArray items;
  for (qsizetype i = 0; i < message.attachments.size() && i < 16; ++i) {
    const auto a = message.attachments[i].toMap();
    if (conversationImagePath("", a.value("asset").toString()).isEmpty())
      continue;
    items.append(QJsonObject{{"index", int(i)},
                             {"name", a.value("name").toString().left(160)}});
  }
  return items;
}
QJsonObject SharedContent::text(const QString &session, const QJsonObject &p,
                                QString *error) const {
  if (!fields(p, {"message", "revision", "offset"}) ||
      !p["message"].isString() || !p["revision"].isString() ||
      !number(p["offset"], 2147483647)) {
    *error = QStringLiteral("正文读取参数无效。");
    return {};
  }
  const auto *m =
      m_chat.conversationMessage(session, p["message"].toString(), error);
  if (!m)
    return {};
  const auto revision = contentRevision(*m);
  if (p["revision"] != revision) {
    *error = QStringLiteral("内容已更新，请重新读取消息。");
    return {};
  }
  const qsizetype offset = p["offset"].toInteger();
  if (offset > m->content.size() ||
      (offset > 0 && offset < m->content.size() &&
       m->content[offset].isLowSurrogate() &&
       m->content[offset - 1].isHighSurrogate())) {
    *error = QStringLiteral("正文位置无效。");
    return {};
  }
  qsizetype end = qMin(offset + 16384, m->content.size());
  if (end < m->content.size() && end > offset &&
      m->content[end - 1].isHighSurrogate() && m->content[end].isLowSurrogate())
    --end;
  return {{"message", m->id},
          {"revision", revision},
          {"offset", offset},
          {"text", m->content.mid(offset, end - offset)},
          {"total", m->content.size()},
          {"next", end < m->content.size() ? QJsonValue(end) : QJsonValue()}};
}
QJsonObject SharedContent::image(const QString &session, const QJsonObject &p,
                                 QString *error) {
  if (!fields(p, {"message", "revision", "index", "offset", "version"}) ||
      !p["message"].isString() || !p["revision"].isString() ||
      !number(p["index"], 15) || !number(p["offset"], 20 * 1024 * 1024) ||
      (p.contains("version") && !p["version"].isString())) {
    *error = QStringLiteral("图片读取参数无效。");
    return {};
  }
  const auto *m =
      m_chat.conversationMessage(session, p["message"].toString(), error);
  if (!m)
    return {};
  const auto index = p["index"].toInt();
  if (p["revision"] != contentRevision(*m) || index >= m->attachments.size()) {
    *error = QStringLiteral("图片引用已变化，请重新同步。");
    return {};
  }
  const auto a = m->attachments[index].toMap();
  const auto directory = m_chat.conversationDirectory(session);
  const auto path =
      conversationImagePath(directory, a.value("asset").toString());
  const QFileInfo info(path), root(directory + "/attachments");
  const auto canonical = info.canonicalFilePath(),
             canonicalRoot = root.canonicalFilePath();
  // 历史本机URL、包外引用、符号链接跳转和非持久附件均不开放。
  if (path.isEmpty() || canonicalRoot.isEmpty() || canonical != path ||
      !canonical.startsWith(canonicalRoot + "/") || !info.isFile() ||
      info.size() <= 0 || info.size() > 20 * 1024 * 1024) {
    *error = QStringLiteral("图片尚未保存在此会话内或无法读取。");
    return {};
  }
  const auto asset = a.value("asset").toString();
  const auto key = QString::fromUtf8(
      QJsonDocument(QJsonArray{session, m->id, p["revision"], index})
          .toJson(QJsonDocument::Compact));
  if (m_failures.contains(key)) {
    if (QDateTime::currentMSecsSinceEpoch() - m_failures[key].second < 2000) {
      *error = m_failures[key].first;
      return {};
    }
    m_failures.remove(key);
  }
  if (!m_images.contains(key)) {
    if (m_pending.contains(key))
      return {{"state", 0}};
    if (m_pending.size() >= 2) {
      *error = QStringLiteral("图片正在准备，请稍后重试。");
      return {};
    }
    m_pending.insert(key);
    auto *watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, this,
            [this, key, watch] {
              m_pending.remove(key);
              const auto r = watch->result();
              watch->deleteLater();
              if (!r.value("error").toString().isEmpty()) {
                if (m_failures.size() >= 32)
                  m_failures.clear();
                m_failures.insert(key, {r.value("error").toString(),
                                        QDateTime::currentMSecsSinceEpoch()});
                return;
              }
              const auto bytes = r.value("bytes").toByteArray();
              while (!m_images.isEmpty() &&
                     (m_images.size() >= 3 ||
                      m_bytes + bytes.size() > 32 * 1024 * 1024)) {
                auto oldest = m_images.begin();
                for (auto it = m_images.begin(); it != m_images.end(); ++it)
                  if (it->touched < oldest->touched)
                    oldest = it;
                m_bytes -= oldest->bytes.size();
                m_images.erase(oldest);
              }
              m_bytes += bytes.size();
              m_images.insert(
                  key,
                  ImageSnapshot{bytes, r.value("type").toString(), ++m_reads});
            });
    // 文件读取、格式检查和完整摘要校验均在后台；发布不可变快照，分段不依赖mtime。
    watch->setFuture(QtConcurrent::run([path, asset]() -> QVariantMap {
      QFile f(path);
      const QFileInfo initial(path);
      if (initial.canonicalFilePath() != path || !f.open(QIODevice::ReadOnly) ||
          f.size() <= 0 || f.size() > 20 * 1024 * 1024)
        return {{"error", QStringLiteral("无法读取会话图片。")}};
      const auto bytes = f.read(20 * 1024 * 1024 + 1);
      if (bytes.size() != initial.size() ||
          QString::fromLatin1(
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                  .toHex()) != asset)
        return {{"error", QStringLiteral("图片内容与会话摘要不一致。")}};
      QBuffer buffer;
      buffer.setData(bytes);
      buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer);
      const auto format = reader.format().toLower();
      const auto dim = reader.size();
      const QHash<QByteArray, QString> types{{"png", "image/png"},
                                             {"jpeg", "image/jpeg"},
                                             {"jpg", "image/jpeg"},
                                             {"webp", "image/webp"},
                                             {"gif", "image/gif"}};
      if (!types.contains(format) || dim.isEmpty() || dim.width() > 16384 ||
          dim.height() > 16384 ||
          qint64(dim.width()) * dim.height() > 32 * 1024 * 1024)
        return {{"error", QStringLiteral("共享图片格式或像素尺寸不支持。")}};
      return {{"bytes", bytes}, {"type", types[format]}};
    }));
    return {{"state", 0}};
  }
  auto &snapshot = m_images[key];
  snapshot.touched = ++m_reads;
  const qint64 offset = p["offset"].toInteger(), total = snapshot.bytes.size();
  if (offset > total || (offset > 0 && p["version"] != asset)) {
    *error = QStringLiteral("图片已变化，请重新打开。");
    return {};
  }
  const auto data = snapshot.bytes.mid(offset, 128 * 1024);
  const auto next = offset + data.size();
  return {{"state", 1},
          {"message", m->id},
          {"index", index},
          {"offset", offset},
          {"version", asset},
          {"type", snapshot.type},
          {"data", QString::fromLatin1(data.toBase64())},
          {"total", total},
          {"next", next < total ? QJsonValue(next) : QJsonValue()}};
}
} // namespace exagent::sharing
