#include "../viewer_registry.h"
#include "../viewer_data.h"
#include <QPdfDocument>
#include <QPdfSelection>
#include <QBuffer>
#include <QMutex>
#include <QMutexLocker>
#include <QUuid>
namespace exagent {
namespace {
QMutex mutex; QHash<QString,QByteArray> documents;
QByteArray snapshot(const QString &token) { QMutexLocker lock(&mutex);return documents.value(token); }
class Plugin final : public WorkspaceViewerPlugin {
public:
    ViewerDescriptor descriptor() const override { return {"pdf","PDF","WorkspacePdfPlugin.qml",{"pdf"},ViewerDescriptor::Managed,64*1024*1024}; }
    QVariantMap load(const QByteArray &bytes,const QString &) const override {return WorkspacePdfProvider::registerPdf(bytes);}
};
}
std::unique_ptr<WorkspaceViewerPlugin> makePdfViewer() {return std::make_unique<Plugin>();}
WorkspacePdfProvider::WorkspacePdfProvider():QQuickImageProvider(QQuickImageProvider::Image,QQuickImageProvider::ForceAsynchronousImageLoading) {}
QVariantMap WorkspacePdfProvider::registerPdf(const QByteArray &bytes) {
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QPdfDocument document;document.load(&buffer);
    if(document.status()!=QPdfDocument::Status::Ready || document.pageCount()<=0 || document.pageCount()>2000) return {{"error",QStringLiteral("PDF 无法读取、已加密或超过 2000 页限制。")}};
    QVariantList pages;
    for(int i=0;i<document.pageCount();++i) {auto size=document.pagePointSize(i);pages.append(QVariantMap{{"width",size.width()},{"height",size.height()}});}
    const auto token=QUuid::createUuid().toString(QUuid::Id128);
    {QMutexLocker lock(&mutex);documents.insert(token,bytes);}
    return {{"token",token},{"pages",pages},{"pageCount",document.pageCount()}};
}
void WorkspacePdfProvider::release(const QString &token) {if(!token.isEmpty()) {QMutexLocker lock(&mutex);documents.remove(token);}}
QString WorkspacePdfProvider::text(const QString &token,int page) {
    QBuffer buffer;buffer.setData(snapshot(token));buffer.open(QIODevice::ReadOnly);QPdfDocument document;document.load(&buffer);
    if(page<0 || page>=document.pageCount()) return {};
    return document.getAllText(page).text().left(1024*1024);
}
QImage WorkspacePdfProvider::requestImage(const QString &id,QSize *size,const QSize &requested) {
    const auto parts=id.split('/');if(parts.size()!=2) return {};bool ok=false;const int page=parts[1].toInt(&ok);if(!ok) return {};
    const auto bytes=snapshot(parts[0]);
    // 页签关闭后的排队图片请求按取消处理，不产生解码错误或继续读取文件。
    if(bytes.isEmpty()) { QImage cancelled(1,1,QImage::Format_ARGB32);cancelled.fill(Qt::transparent);if(size) *size=cancelled.size();return cancelled; }
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QPdfDocument document;document.load(&buffer);
    if(page<0 || page>=document.pageCount()) return {};
    const auto natural=document.pagePointSize(page).toSize();if(size) *size=natural;
    auto target=natural.scaled(requested.isValid() ? requested : QSize(1200,1600),Qt::KeepAspectRatio);
    if(target.width()>4096 || target.height()>4096) target=target.scaled(QSize(4096,4096),Qt::KeepAspectRatio);
    if(qint64(target.width())*target.height()>8*1024*1024) target=target.scaled(QSize(2800,2800),Qt::KeepAspectRatio);
    // 首次布局可能暂时只有一像素，保持长宽有效，避免把布局过渡误报为解码失败。
    target.setWidth(qMax(1,target.width()));target.setHeight(qMax(1,target.height()));
    return document.render(page,target);
}
}
