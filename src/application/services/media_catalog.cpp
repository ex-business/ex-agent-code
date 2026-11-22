#include "media_catalog.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QUuid>
#include <QCollator>
#include <QSet>
#include <algorithm>

namespace exagent {
MediaCatalog::MediaCatalog(const QString &directory, QObject *parent) : QObject(parent), m_path(directory + "/media-resources.json")
{
    m_writer.setMaxThreadCount(1);
    QFile file(m_path);
    if (file.exists()) {
        QJsonParseError error;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) m_writable = false;
        else {
            const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            const auto entries = doc.object().value("resources").toArray();
            if (error.error != QJsonParseError::NoError || doc.object().value("version").toInt() != 1 || entries.size() > 2000) m_writable = false;
            else for (const auto &value : entries) {
                const auto item = value.toObject();
                m_files.insert(item.value("id").toString(), {item.value("path").toString(), item.value("root").toString(), item.value("label").toString(), item.value("width").toInt(), item.value("height").toInt()});
            }
        }
    }
    m_timer.setSingleShot(true); m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &MediaCatalog::save);
}
MediaCatalog::~MediaCatalog()
{
    m_timer.stop(); m_writer.waitForDone();
    // 退出时先完成旧快照，再写入最后一次登记，不能依赖已停止的GUI事件循环补写。
    m_saving = false; if (m_dirty) save(); m_writer.waitForDone();
}
MediaImport MediaCatalog::inspectFolder(const QString &folder, const std::shared_ptr<std::atomic_bool> &cancel)
{
    MediaImport result;
    const auto root = QFileInfo(folder).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) { result.error = QStringLiteral("图片目录不存在或无法读取"); return result; }
    auto files = QDir(root).entryInfoList({"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.PNG", "*.JPG", "*.JPEG"}, QDir::Files | QDir::Readable, QDir::NoSort);
    if (files.size() > 500) { result.error = QStringLiteral("图片序列最多500帧，请选择较小的目录"); return result; }
    QCollator collator; collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&](const QFileInfo &a, const QFileInfo &b) { return collator.compare(a.fileName(), b.fileName()) < 0; });
    for (const auto &file : files) {
        if (cancel->load()) return {};
        const auto path = file.canonicalFilePath();
        if (!path.startsWith(root + '/') || file.size() > 64 * 1024 * 1024) { result.error = QStringLiteral("图片路径越界或文件超过64MiB"); return result; }
        QImageReader reader(path); const auto size = reader.size();
        if (!reader.canRead() || size.isEmpty() || qint64(size.width()) * size.height() > 16 * 1024 * 1024) {
            result.error = QStringLiteral("图片损坏、不支持或像素数超过16Mi：") + file.fileName(); return result;
        }
        result.files.append({path, root, file.fileName(), size.width(), size.height()});
    }
    if (result.files.isEmpty()) result.error = QStringLiteral("目录中没有可播放的图片");
    return result;
}
QJsonArray MediaCatalog::registerFiles(const QList<MediaFile> &files, QString *error)
{
    QHash<QString, QString> byPath;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) byPath.insert(it->path, it.key());
    QSet<QString> additional;
    for (const auto &file : files) if (!byPath.contains(file.path)) additional.insert(file.path);
    if (!m_writable || m_files.size() + additional.size() > 2000) { *error = QStringLiteral("资源目录不可写或资源数量超过2000"); return {}; }
    QJsonArray refs;
    for (const auto &file : files) {
        QString id = byPath.value(file.path);
        if (id.isEmpty()) { id = QUuid::createUuid().toString(QUuid::WithoutBraces); byPath.insert(file.path, id); }
        m_files.insert(id, file);
        refs.append(QJsonObject{{"resourceId", id}, {"label", file.label}, {"width", file.width}, {"height", file.height}});
    }
    m_dirty = true; m_timer.start(); return refs;
}
QUrl MediaCatalog::source(const QString &id) const
{
    const auto file = m_files.value(id);
    if (file.path.isEmpty() || file.root.isEmpty()) return {};
    // 再次解析规范路径，拒绝登记后被替换成目录外符号链接的资源。
    const auto canonical = QFileInfo(file.path).canonicalFilePath();
    if (canonical != file.path || !canonical.startsWith(file.root + '/')) return {};
    return QUrl::fromLocalFile(canonical);
}
void MediaCatalog::flush() { m_timer.stop(); if (m_dirty && !m_saving) save(); }
void MediaCatalog::save()
{
    if (m_saving || !m_dirty || !m_writable) return;
    QJsonArray resources;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        const auto &f = it.value(); resources.append(QJsonObject{{"id", it.key()}, {"path", f.path}, {"root", f.root}, {"label", f.label}, {"width", f.width}, {"height", f.height}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"resources", resources}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 2 * 1024 * 1024) { emit failure(QStringLiteral("媒体资源清单超过2MiB，未写入")); return; }
    m_dirty = false; m_saving = true;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        m_saving = false;
        if (!watcher->result()) { m_writable = false; emit failure(QStringLiteral("媒体资源清单保存失败，已有文件保留")); }
        watcher->deleteLater(); if (m_dirty) m_timer.start();
    });
    watcher->setFuture(QtConcurrent::run(&m_writer, [path = m_path, bytes] {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return file.write(bytes) == bytes.size() && file.commit();
    }));
}
}
