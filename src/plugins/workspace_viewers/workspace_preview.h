#pragma once
#include <QFutureWatcher>
#include <QQuickImageProvider>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace exagent {
void installWorkspaceViewers(QQmlEngine &engine);
// 工作区图片只通过不透明资源标识进入界面，解码由 Qt 图片线程执行。
class WorkspaceImageProvider final : public QQuickImageProvider {
  public:
    WorkspaceImageProvider();
    static QVariantMap registerFile(const QString &path, const QString &root, QString *error);
    static void release(const QUrl &url);
    static bool contains(const QUrl &url);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
class WorkspaceMarkdownModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source MEMBER m_source NOTIFY inputChanged)
    Q_PROPERTY(QString filePath MEMBER m_filePath NOTIFY inputChanged)
    Q_PROPERTY(QString rootPath MEMBER m_root NOTIFY inputChanged)
    Q_PROPERTY(bool dark MEMBER m_dark NOTIFY inputChanged)
    Q_PROPERTY(int previewWidth MEMBER m_width NOTIFY inputChanged)
    Q_PROPERTY(bool active MEMBER m_active NOTIFY inputChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed)
  public:
    enum State { Idle = 0, Loading = 1, Ready = 2, Failed = 3 };
    Q_ENUM(State)
    explicit WorkspaceMarkdownModel(QObject *parent = nullptr);
    ~WorkspaceMarkdownModel() override;
    QVariantList blocks() const { return m_blocks; }
    QString error() const { return m_error; }
    int state() const { return m_state; }
  signals:
    void inputChanged();
    void changed();

  private:
    void prepare();
    void releaseImages();
    QString m_source, m_filePath, m_root, m_error, m_shownFile, m_shownRoot;
    int m_width = 480;
    bool m_dark = false, m_active = true;
    QVariantList m_blocks;
    QList<QUrl> m_images;
    QHash<QString, QVariantMap> m_cachedImages;
    QTimer m_delay;
    QFileSystemWatcher m_imageWatch;
    bool m_imagesDirty=false;
    QFutureWatcher<QVariantList> m_job;
    quint64 m_generation = 0, m_running = 0;
    int m_state = Idle;
};
} // namespace exagent
