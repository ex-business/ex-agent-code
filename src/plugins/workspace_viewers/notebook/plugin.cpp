#include "../viewer_registry.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QImageReader>
#include <QBuffer>
namespace exagent {
namespace {
QString joined(const QJsonValue &value) { if(value.isString()) return value.toString(); QString result; for(const auto &line:value.toArray()) result+=line.toString(); return result; }
class Plugin final : public WorkspaceViewerPlugin {
public:
    ViewerDescriptor descriptor() const override { return {"notebook","Jupyter","WorkspaceNotebookPlugin.qml",{"ipynb"},ViewerDescriptor::Managed,8*1024*1024}; }
    QVariantMap load(const QByteArray &bytes,const QString &) const override {
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error); const auto object=doc.object();
        if(error.error!=QJsonParseError::NoError || object.value("nbformat").toInt()!=4 || !object.value("cells").isArray()) return {{"error",QStringLiteral("仅支持格式完整的 nbformat 4 Notebook。")}};
        const auto input=object.value("cells").toArray(); if(input.size()>5000) return {{"error",QStringLiteral("Notebook 超过 5000 个单元格。")}};
        const auto language=object.value("metadata").toObject().value("language_info").toObject().value("name").toString("python");
        QVariantList cells; qint64 imagePixels=0;
        for (const auto &value:input) {
            const auto cell=value.toObject(); const auto type=cell.value("cell_type").toString();
            if (type!="code" && type!="markdown" && type!="raw") return {{"error",QStringLiteral("Notebook 包含不支持的单元格类型。")}};
            if(joined(cell.value("source")).size()>262144) return {{"error",QStringLiteral("Notebook 单元格源码超过 256 Ki 字符限制。")}};
            QVariantList outputs; int count=0; qsizetype outputCharacters=0;
            for(const auto &out:cell.value("outputs").toArray()) {
                if(++count>100) return {{"error",QStringLiteral("单元格输出超过 100 项。")}};
                const auto output=out.toObject(),data=output.value("data").toObject();
                QString text=joined(output.value("text")); if(text.isEmpty()) text=joined(data.value("text/plain"));
                if(output.value("output_type")=="error") { QStringList lines; for(const auto &line:output.value("traceback").toArray()) lines.append(line.toString()); text=lines.join('\n'); }
                outputCharacters+=text.size(); if(outputCharacters>512*1024) return {{"error",QStringLiteral("单元格文本输出超过 512 Ki 字符限制。")}};
                if(!text.isEmpty()) outputs.append(QVariantMap{{"kind",1},{"text",text.size()>262144 ? text.left(262144)+QStringLiteral("\n…输出超过 256 Ki 字符，预览已截断…") : text}});
                const QString mime=data.contains("image/png") ? "image/png" : "image/jpeg";
                const auto encoded=joined(data.value(mime));
                if(!encoded.isEmpty()) {
                    bool accepted=false;
                    if(encoded.size()<=4*1024*1024) {
                        auto image=QByteArray::fromBase64(encoded.toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
                        QBuffer buffer(&image);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,mime=="image/png" ? "png" : "jpeg");const auto size=reader.size();
                        const qint64 pixels=qint64(size.width())*size.height();
                        if(size.isValid() && pixels<=8*1024*1024 && imagePixels+pixels<=32*1024*1024) {
                            imagePixels+=pixels; accepted=true;
                            outputs.append(QVariantMap{{"kind",2},{"url",QString("data:")+mime+";base64,"+QString::fromLatin1(image.toBase64())}});
                        }
                    }
                    if(!accepted) outputs.append(QVariantMap{{"kind",3},{"text",QStringLiteral("图片输出无效或超过预览资源限制")}});
                }
                // 不渲染活动 HTML、SVG 和 JavaScript 输出，预览不能隐式启动内核或网络请求。
                if(data.contains("text/html") || data.contains("application/javascript") || data.contains("image/svg+xml")) outputs.append(QVariantMap{{"kind",3},{"text",QStringLiteral("活动 HTML / SVG / JavaScript 输出已禁用")}});
            }
            cells.append(QVariantMap{{"kind",type=="code" ? 1 : type=="markdown" ? 2 : 3},{"source",joined(cell.value("source"))},{"language",language},{"execution",cell.value("execution_count").toVariant()},{"outputs",outputs}});
        }
        return {{"cells",cells},{"language",language},{"cellCount",cells.size()}};
    }
};
}
std::unique_ptr<WorkspaceViewerPlugin> makeNotebookViewer() {return std::make_unique<Plugin>();}
}
