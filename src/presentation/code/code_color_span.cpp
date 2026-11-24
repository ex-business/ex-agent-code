#include "code_color_span.h"
namespace exagent {
namespace {
QString escape(QStringView text)
{
    return text.toString().toHtmlEscaped().replace('\t', "    ").replace(' ', "&nbsp;");
}
}
QString renderColorSpans(QStringView text, const CodeColorSpan *spans, int count, bool dark)
{
    QString output;
    int end = 0;
    for (int i = 0; i < count; ++i) {
        const auto &span = spans[i];
        output += escape(text.mid(end, span.offset - end));
        output += "<font color=\"" + QColor::fromRgba(dark ? span.dark : span.light).name() + "\">"
            + escape(text.mid(span.offset, span.length)) + "</font>";
        end = span.offset + span.length;
    }
    return output + escape(text.mid(end));
}
}
