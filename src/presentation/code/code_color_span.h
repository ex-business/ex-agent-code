#pragma once
#include <QColor>
#include <QStringView>
namespace exagent {
struct CodeColorSpan {
    int offset, length;
    QRgb light, dark;
};

QString renderColorSpans(QStringView text, const CodeColorSpan *spans, int count, bool dark);
}
