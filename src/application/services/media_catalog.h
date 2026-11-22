#pragma once
#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QTimer>
#include <QThreadPool>
#include <QUrl>
#include <atomic>
#include <memory>

namespace exagent {
struct MediaFile { QString path, root, label; int width = 0, height = 0; };
struct MediaImport { QList<MediaFile> files; QString error; };
// 控制帧只传随机资源ID；路径仅由用户选择目录后在宿主登记，不接受后台任意文件URL。
class MediaCatalog final : public QObject {
    Q_OBJECT
public:
    explicit MediaCatalog(const QString &directory, QObject *parent = nullptr);
    ~MediaCatalog() override;
    static MediaImport inspectFolder(const QString &folder, const std::shared_ptr<std::atomic_bool> &cancel);
    QJsonArray registerFiles(const QList<MediaFile> &files, QString *error);
    QUrl source(const QString &id) const;
    void flush();
signals:
    void failure(QString reason);
private:
    void save();
    QString m_path;
    QHash<QString, MediaFile> m_files;
    QTimer m_timer;
    QThreadPool m_writer;
    bool m_writable = true, m_saving = false, m_dirty = false;
};
}
