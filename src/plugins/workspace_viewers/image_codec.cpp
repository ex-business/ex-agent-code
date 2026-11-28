#include "image_codec.h"
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <cmath>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif
namespace exagent::workspace_image {
namespace {
QSize targetSize(QSize size, QSize requested, bool vector) {
  if (!requested.isValid())
    requested = QSize(960, 720);
  const double scale = std::min(
      {vector ? 8.0 : 1.0, double(requested.width()) / size.width(),
       double(requested.height()) / size.height(),
       8192.0 / std::max(size.width(), size.height()),
       std::sqrt(8.0 * 1024 * 1024 / (qint64(size.width()) * size.height()))});
  return QSize(qMax(1, int(size.width() * scale)),
               qMax(1, int(size.height() * scale)));
}
bool safeSvg(const QByteArray &bytes) {
  // 只接收静态矢量元素；外链、脚本、实体及嵌入对象不能借预览读取文件或访问网络。
  const QSet<QString> tags = {
      "svg",      "g",       "defs",           "title",          "desc",
      "path",     "rect",    "circle",         "ellipse",        "line",
      "polyline", "polygon", "text",           "tspan",          "textPath",
      "use",      "symbol",  "linearGradient", "radialGradient", "stop",
      "clipPath", "mask",    "pattern",        "marker"};
  QXmlStreamReader xml(bytes);
  int count = 0, depth = 0;
  bool svg = false;
  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::DTD ||
        token == QXmlStreamReader::EntityReference ||
        token == QXmlStreamReader::ProcessingInstruction)
      return false;
    if (token == QXmlStreamReader::StartElement) {
      if (++count > 100000 || ++depth > 128 ||
          !tags.contains(xml.name().toString()) ||
          (!xml.namespaceUri().isEmpty() &&
           xml.namespaceUri() != QStringLiteral("http://www.w3.org/2000/svg")))
        return false;
      if (!svg) {
        if (xml.name() != QStringLiteral("svg"))
          return false;
        svg = true;
      }
      for (const auto &attr : xml.attributes()) {
        const auto name = attr.name().toString().toLower(),
                   value = attr.value().toString().trimmed();
        if (name.startsWith("on") || name == "base" ||
            (name == "href" && !value.startsWith('#')) || value.contains('@'))
          return false;
        static const QRegularExpression urls(
            "url\\s*\\(([^)]*)\\)", QRegularExpression::CaseInsensitiveOption);
        auto matches = urls.globalMatch(value);
        while (matches.hasNext()) {
          auto ref = matches.next().captured(1).trimmed();
          ref.remove(QRegularExpression("^[\"']|[\"']$"));
          if (!ref.startsWith('#'))
            return false;
        }
      }
    } else if (token == QXmlStreamReader::EndElement)
      --depth;
  }
  return svg && !xml.hasError();
}
#ifdef Q_OS_MACOS
CGImageSourceRef heicSource(const QByteArray &bytes) {
  auto data = CFDataCreate(kCFAllocatorDefault,
                           reinterpret_cast<const UInt8 *>(bytes.constData()),
                           bytes.size());
  auto source = data ? CGImageSourceCreateWithData(data, nullptr) : nullptr;
  if (data)
    CFRelease(data);
  if (source) {
    const auto type = CGImageSourceGetType(source);
    if (!type || (!CFEqual(type, CFSTR("public.heic")) &&
                  !CFEqual(type, CFSTR("public.heif")))) {
      CFRelease(source);
      return nullptr;
    }
  }
  return source;
}
QSize heicSize(CGImageSourceRef source) {
  auto properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  int w = 0, h = 0, o = 1;
  if (properties) {
    auto read = [&](CFStringRef key, int *value) {
      auto n = CFDictionaryGetValue(properties, key);
      if (n && CFGetTypeID(n) == CFNumberGetTypeID())
        CFNumberGetValue(static_cast<CFNumberRef>(n), kCFNumberIntType, value);
    };
    read(kCGImagePropertyPixelWidth, &w);
    read(kCGImagePropertyPixelHeight, &h);
    read(kCGImagePropertyOrientation, &o);
    CFRelease(properties);
  }
  return o >= 5 && o <= 8 ? QSize(h, w) : QSize(w, h);
}
#endif
} // namespace
bool inspect(QFile &file, Metadata *metadata, QString *error) {
  file.seek(0);
  QImageReader reader(&file);
  reader.setAutoTransform(true);
  auto format = reader.format().toLower();
  if (format == "svg" || file.peek(1024).contains("<svg")) {
    file.seek(0);
    const auto data = file.read(32 * 1024 * 1024 + 1);
    if (data.size() > 32 * 1024 * 1024 || !safeSvg(data)) {
      *error = QStringLiteral("SVG 含不支持的元素或外部引用。");
      return false;
    }
    QSvgRenderer svg(data);
    metadata->codec = 2;
    metadata->size = svg.defaultSize();
    if (!svg.isValid())
      metadata->size = {};
  } else if (QList<QByteArray>{"png", "jpeg", "jpg", "webp", "bmp", "gif"}
                 .contains(format)) {
    metadata->codec = 1;
    metadata->size = reader.size();
    if (int(reader.transformation()) & 4)
      metadata->size.transpose();
  } else {
#ifdef Q_OS_MACOS
    file.seek(0);
    const auto data = file.read(32 * 1024 * 1024 + 1);
    auto source = data.size() <= 32 * 1024 * 1024 ? heicSource(data) : nullptr;
    if (source) {
      metadata->codec = 3;
      metadata->size = heicSize(source);
      CFRelease(source);
    }
#endif
  }
  file.seek(0);
  if (!metadata->size.isValid() ||
      qint64(metadata->size.width()) * metadata->size.height() >
          32 * 1024 * 1024) {
    *error = QStringLiteral("图片格式不受支持或超过 32 Mi 像素限制。");
    return false;
  }
  return true;
}
QImage decode(QFile &file, const QSize &requested) {
  Metadata meta;
  QString error;
  if (!inspect(file, &meta, &error))
    return {};
  const auto size = targetSize(meta.size, requested, meta.codec == 2);
  if (meta.codec == 1) {
    QImageReader reader(&file);
    reader.setAutoTransform(true);
    auto scaled = size;
    if (int(reader.transformation()) & 4)
      scaled.transpose();
    reader.setScaledSize(scaled);
    return reader.read();
  }
  const auto data = file.read(32 * 1024 * 1024 + 1);
  if (data.size() > 32 * 1024 * 1024)
    return {};
  if (meta.codec == 2) {
    // 文件可能在尺寸检查后被外部改写，最终进入渲染器的同一份字节必须再次验证。
    if (!safeSvg(data))
      return {};
    QSvgRenderer renderer(data);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    return image;
  }
#ifdef Q_OS_MACOS
  auto source = heicSource(data);
  if (!source)
    return {};
  int maxSize = qMax(size.width(), size.height());
  auto number = CFNumberCreate(nullptr, kCFNumberIntType, &maxSize);
  const void *keys[] = {kCGImageSourceCreateThumbnailFromImageAlways,
                        kCGImageSourceCreateThumbnailWithTransform,
                        kCGImageSourceThumbnailMaxPixelSize};
  const void *values[] = {kCFBooleanTrue, kCFBooleanTrue, number};
  auto options = CFDictionaryCreate(nullptr, keys, values, 3,
                                    &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
  auto image = CGImageSourceCreateThumbnailAtIndex(source, 0, options);
  CFRelease(options);
  CFRelease(number);
  CFRelease(source);
  if (!image)
    return {};
  QImage output(int(CGImageGetWidth(image)), int(CGImageGetHeight(image)),
                QImage::Format_RGBA8888_Premultiplied);
  output.fill(Qt::transparent);
  auto color = CGColorSpaceCreateDeviceRGB();
  auto context =
      CGBitmapContextCreate(output.bits(), output.width(), output.height(), 8,
                            output.bytesPerLine(), color,
                            CGBitmapInfo(kCGImageAlphaPremultipliedLast) |
                                CGBitmapInfo(kCGBitmapByteOrder32Big));
  CGColorSpaceRelease(color);
  // ImageIO 已按 EXIF 生成方向正确的缩略图；位图行序直接交给
  // QImage，不能再垂直翻转。
  if (context) {
    CGContextDrawImage(
        context, CGRectMake(0, 0, output.width(), output.height()), image);
    CGContextRelease(context);
  }
  CFRelease(image);
  return context ? output : QImage{};
#else
  return {};
#endif
}
} // namespace exagent::workspace_image
