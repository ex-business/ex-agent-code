#include "workspace_preview.h"
#include "viewer_data.h"
#include "presentation/text/markdown_formatter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <QtConcurrent>
#include <cmath>

namespace exagent {
namespace {
struct ImageFile {
    QString path, root;
};
QMutex imageMutex;
QHash<QString, ImageFile> imageFiles;
QString key(const QUrl &url) { return url.path().mid(1).section('?', 0, 0); }
// SVG 等可含外部引用的格式不进入本机栅格查看通路。
bool inspect(QFile &file, const ImageFile &entry, QImageReader &reader, QString *error) {
    const auto canonical = QFileInfo(entry.path).canonicalFilePath();
    if (entry.root.isEmpty() || QFileInfo(entry.root).canonicalFilePath() != entry.root ||
        canonical.isEmpty() ||
        !(canonical.startsWith(entry.root.endsWith('/') ? entry.root : entry.root + '/')) ||
        !file.open(QIODevice::ReadOnly) || QFileInfo(entry.path).canonicalFilePath() != canonical ||
        file.size() > 32 * 1024 * 1024) {
        *error = QStringLiteral("图片不可读取、超出工作区或超过 32 MiB 限制。");
        return false;
    }
    reader.setDevice(&file);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (!QList<QByteArray>{"png", "jpeg", "jpg", "webp", "bmp", "gif"}.contains(format)) {
        *error = QStringLiteral("图片格式不受支持。");
        return false;
    }
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 32 * 1024 * 1024) {
        *error = QStringLiteral("图片格式不受支持或超过 32 Mi 像素限制。");
        return false;
    }
    return true;
}
} // namespace
WorkspaceImageProvider::WorkspaceImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, ForceAsynchronousImageLoading) {}
QVariantMap WorkspaceImageProvider::registerFile(const QString &path, const QString &root, QString *error) {
    ImageFile entry{path, root};
    QFile file(path);
    QImageReader reader;
    if (!inspect(file, entry, reader, error))
        return {};
    auto dimensions = reader.size();
    if (int(reader.transformation()) & 4)
        dimensions.transpose();
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QMutexLocker lock(&imageMutex);
        imageFiles.insert(id, entry);
    }
    return {{"url", QUrl("image://workspace-images/" + id)},
            {"width", dimensions.width()},
            {"height", dimensions.height()},
            {"name", QFileInfo(path).fileName()},
            {"path", path},
            {"bytes", file.size()},
            {"modified", QFileInfo(path).lastModified().toMSecsSinceEpoch()}};
}
void WorkspaceImageProvider::release(const QUrl &url) {
    QMutexLocker lock(&imageMutex);
    imageFiles.remove(key(url));
}
bool WorkspaceImageProvider::contains(const QUrl &url) {
    QMutexLocker lock(&imageMutex);
    return imageFiles.contains(key(url));
}
QImage WorkspaceImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    if (id == "bootstrap") {
        QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
        pixel.fill(Qt::transparent);
        if (size)
            *size = pixel.size();
        return pixel;
    }
    ImageFile entry;
    {
        QMutexLocker lock(&imageMutex);
        entry = imageFiles.value(id.section('?', 0, 0));
    }
    QFile file(entry.path);
    QImageReader reader;
    QString error;
    if (entry.root.isEmpty() || !inspect(file, entry, reader, &error))
        return {};
    const auto natural = reader.size();
    // 富文本取图没有请求尺寸，按内嵌 480×360 的两倍像素准备，避免多图保留大位图。
    auto target = requestedSize.isValid() ? requestedSize : QSize(960, 720);
    if (int(reader.transformation()) & 4)
        target.transpose();
    const double scale =
        std::min({1.0, double(target.width()) / natural.width(), double(target.height()) / natural.height(),
                  8192.0 / std::max(natural.width(), natural.height()),
                  std::sqrt(8.0 * 1024 * 1024 / (qint64(natural.width()) * natural.height()))});
    reader.setScaledSize(
        QSize(std::max(1, int(natural.width() * scale)), std::max(1, int(natural.height() * scale))));
    auto image = reader.read();
    if (size)
        *size = image.size();
    return image;
}
void installWorkspaceViewers(QQmlEngine &engine) {
    Q_ASSERT(engine.thread() == QThread::currentThread());
    engine.addImageProvider("workspace-images", new WorkspaceImageProvider);
    engine.addImageProvider("workspace-pdf", new WorkspacePdfProvider);
    // 富文本首次取图可能发生在场景同步阶段，先在 GUI 线程建立 Qt 异步图片读取器。
    // 只加载内置透明像素，不读取用户目录，也不触发网络请求。
    QQmlComponent component(&engine);
    component.loadFromModule("QtQuick", "Image");
    auto *bootstrap =
        component.createWithInitialProperties({{"asynchronous", true},
                                               {"cache", false},
                                               {"visible", false},
                                               {"source", QUrl("image://workspace-images/bootstrap")}});
    if (bootstrap)
        bootstrap->setParent(&engine);
    else
        qWarning().noquote() << "工作区图片读取器初始化失败：" << component.errorString();
}
WorkspaceMarkdownModel::WorkspaceMarkdownModel(QObject *parent) : QObject(parent) {
    m_delay.setSingleShot(true);
    m_delay.setInterval(120);
    connect(this, &WorkspaceMarkdownModel::inputChanged, this, [this] {
        ++m_generation;
        // 换文件立即撤下旧预览；同一文档的连续输入则保留上一份完整结果。
        if (m_filePath != m_shownFile || m_root != m_shownRoot) {
            m_shownFile = m_filePath;
            m_shownRoot = m_root;
            releaseImages();
            m_blocks.clear();
            emit changed();
        }
        if (m_active)
            m_delay.start();
        else
            m_delay.stop();
    });
    connect(&m_delay, &QTimer::timeout, this, &WorkspaceMarkdownModel::prepare);
    connect(&m_job, &QFutureWatcher<QVariantList>::finished, this, [this] {
        if (!m_active)
            return;
        if (m_running != m_generation) {
            prepare();
            return;
        }
        auto blocks = m_job.result();
        QHash<QString, QVariantMap> nextImages;
        QList<QUrl> imageUrls;
        // 只在宿主登记工作区内的本机图片；外部地址及越界引用不自动加载。
        const QRegularExpression imageTag("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression source("\\bsrc=\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        for (auto &entry : blocks) {
            auto block = entry.toMap();
            auto html = block.value("html").toString();
            auto matches = imageTag.globalMatch(html);
            QList<QRegularExpressionMatch> tags;
            while (matches.hasNext())
                tags.prepend(matches.next());
            for (const auto &tag : tags) {
                auto reference = source.match(tag.captured()).captured(1);
                reference.replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "\'");
                const auto url = QUrl::fromLocalFile(m_filePath).resolved(QUrl(reference));
                QString error;
                QVariantMap info;
                if (url.isLocalFile()) {
                    const auto path = QDir::cleanPath(url.toLocalFile());
                    // 新登记和缓存复用共用单次预览的资源上限。
                    if (nextImages.contains(path) || nextImages.size() < 32) {
                        const QFileInfo file(path);
                        info = nextImages.value(path, m_cachedImages.value(path));
                        if (info.value("bytes").toLongLong() != file.size() ||
                            info.value("modified").toLongLong() != file.lastModified().toMSecsSinceEpoch() ||
                            !WorkspaceImageProvider::contains(info.value("url").toUrl()))
                            info.clear();
                        if (info.isEmpty() && nextImages.size() < 32)
                            info = WorkspaceImageProvider::registerFile(path, m_root, &error);
                        if (!info.isEmpty())
                            nextImages.insert(path, info);
                    }
                }
                // 图片对象在富文本文档中占一个字符，替代符保持后续表格装饰坐标不变。
                QString replacement = QStringLiteral("▧");
                if (!info.isEmpty()) {
                    const auto local = info.value("url").toUrl();
                    if (!imageUrls.contains(local))
                        imageUrls.append(local);
                    // 固定宽高比例，避免图片异步就绪后再次撑开预览正文。
                    const double scale =
                        std::min({1.0, double(std::max(16, m_width)) / info.value("width").toInt(),
                                  480.0 / info.value("width").toInt(), 360.0 / info.value("height").toInt()});
                    const int width = std::max(1, int(info.value("width").toInt() * scale));
                    const int height = std::max(1, int(info.value("height").toInt() * scale));
                    replacement =
                        QStringLiteral(
                            "<a href=\"%4\"><img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"%5\" /></a>")
                            .arg(local.toString().toHtmlEscaped())
                            .arg(width)
                            .arg(height)
                            .arg(
                                QUrl::fromLocalFile(info.value("path").toString()).toString().toHtmlEscaped())
                            .arg(info.value("name").toString().toHtmlEscaped());
                }
                html.replace(tag.capturedStart(), tag.capturedLength(), replacement);
            }
            // 正文的 150% 行高不应用到图片段落，避免图片下方留下半张图的空白。
            const QRegularExpression paragraph("<p\\b[^>]*>.*?</p>",
                                               QRegularExpression::DotMatchesEverythingOption);
            auto paragraphMatches = paragraph.globalMatch(html);
            QList<QRegularExpressionMatch> imageParagraphs;
            while (paragraphMatches.hasNext()) {
                const auto match = paragraphMatches.next();
                if (match.captured().contains("<img "))
                    imageParagraphs.prepend(match);
            }
            for (const auto &match : imageParagraphs) {
                const auto opening = match.captured().left(match.captured().indexOf('>') + 1);
                auto value = opening;
                value.replace("line-height:150%", "line-height:100%");
                html.replace(match.capturedStart(), opening.size(), value);
            }
            block.insert("html", html);
            entry = block;
        }
        for (const auto &url : m_images)
            if (!imageUrls.contains(url))
                WorkspaceImageProvider::release(url);
        m_images = imageUrls;
        m_cachedImages = nextImages;
        m_blocks = blocks;
        m_state = Ready;
        emit changed();
    });
}
WorkspaceMarkdownModel::~WorkspaceMarkdownModel() { releaseImages(); }
void WorkspaceMarkdownModel::releaseImages() {
    for (const auto &url : m_images)
        WorkspaceImageProvider::release(url);
    m_images.clear();
    m_cachedImages.clear();
}
void WorkspaceMarkdownModel::prepare() {
    if (!m_active || m_job.isRunning())
        return;
    if (m_source.size() > 128 * 1024) {
        releaseImages();
        m_blocks.clear();
        m_error = QStringLiteral("Markdown 超过 128 Ki 字符预览限制，请使用源码视图。");
        m_state = Failed;
        emit changed();
        return;
    }
    m_running = m_generation;
    m_error.clear();
    m_state = Loading;
    emit changed();
    const auto text = m_source;
    const auto dark = m_dark;
    m_job.setFuture(QtConcurrent::run([text, dark] {
        MarkdownFormatter formatter;
        return formatter.format(text, dark);
    }));
}
} // namespace exagent
