#include "../viewer_registry.h"
#include <QStringDecoder>
namespace exagent {
namespace {
class Plugin final : public WorkspaceViewerPlugin {
public:
    ViewerDescriptor descriptor() const override { return {"table","CSV / TSV","WorkspaceTablePlugin.qml",{"csv","tsv"},ViewerDescriptor::Managed,32*1024*1024}; }
    QVariantMap load(const QByteArray &bytes,const QString &extension) const override {
        QStringDecoder decoder(QStringDecoder::Utf8); QString source=decoder(bytes);
        if (decoder.hasError() || source.contains(QChar(0))) return {{"error",QStringLiteral("表格须为 UTF-8 文本。")}};
        if (source.startsWith(QChar(0xfeff))) source.remove(0,1);
        // 分隔符由读取入口附带，TSV 不通过内容猜测，以免误解析带制表符的 CSV 字段。
        const QChar separator=extension=="tsv" ? QChar('\t') : QChar(',');
        QVariantList rows; QStringList row; QString field; bool quoted=false,closed=false,started=false;
        qsizetype cells=0;
        auto appendField=[&]{ row.append(field);field.clear();closed=false;started=false;return ++cells<=2000000 && row.size()<=1000; };
        auto appendRow=[&]{ rows.append(QVariant(row));row.clear();return rows.size()<=100000; };
        for (qsizetype i=0;i<source.size();++i) {
            const QChar c=source[i];
            if (quoted) { if (c=='"') { if (i+1<source.size() && source[i+1]=='"') {field+='"';++i;} else {quoted=false;closed=true;} } else field+=c; }
            else if (c==separator) { if (!appendField()) return limit(); }
            else if (c=='\n' || c=='\r') { if(c=='\r' && i+1<source.size() && source[i+1]=='\n') ++i; if(!appendField() || !appendRow()) return limit(); }
            else if (c=='"' && !started && field.isEmpty() && !closed) {quoted=true;started=true;}
            else {if (closed || c=='"') return {{"error",QStringLiteral("表格引号格式错误。")}};field+=c;started=true;}
            if (field.size()>65536) return limit();
        }
        if (quoted) return {{"error",QStringLiteral("表格存在未闭合的引号。")}};
        if (started || closed || !field.isEmpty() || !row.isEmpty()) {if(!appendField() || !appendRow()) return limit();}
        return {{"rows",rows},{"rowCount",rows.size()},{"cellCount",cells}};
    }
private:
    static QVariantMap limit() {return {{"error",QStringLiteral("表格超过 100000 行、1000 列、200万单元格或单元格 64 Ki 字符限制。")}};}
};
}
std::unique_ptr<WorkspaceViewerPlugin> makeTableViewer() { return std::make_unique<Plugin>(); }
}
