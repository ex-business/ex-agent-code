#include "script_preview.h"
#include <QRegularExpression>

namespace exagent {
QString pythonCommandPreview(const QString &command)
{
    if (command.size() > 16384) return {};
    static const QRegularExpression wrapper(QStringLiteral(R"rx(^(?:(?:/bin/)?(?:zsh|bash|sh) -l?c )"(.*)"$)rx"),
        QRegularExpression::DotMatchesEverythingOption);
    QString inner = command;
    const auto match = wrapper.match(command);
    if (match.hasMatch()) {
        inner.clear();
        const auto payload = match.capturedView(1);
        for (qsizetype i = 0; i < payload.size(); ++i) {
            const auto c = payload[i];
            if (c == '$' || c == '`' || c == '"') return {};
            if (c == '\\' && i + 1 < payload.size() && QStringView(u"\\\"`$").contains(payload[i + 1]))
                inner += payload[++i];
            else inner += c;
        }
    }
    static const QRegularExpression invocation(QStringLiteral(R"rx(^python3? -c '([^']+)'$)rx"));
    const auto literal = invocation.match(inner);
    if (!literal.hasMatch()) return {};
    const QString source = literal.captured(1);
    if (source.contains('\n')) return source;

    // 只拆顶层简单语句。冒号可能引入单行复合语句，贸然拆分会改变缩进含义。
    QString display;
    QChar quote;
    int depth = 0;
    for (qsizetype i = 0; i < source.size(); ++i) {
        const auto c = source[i];
        if (!quote.isNull()) {
            display += c;
            if (c == '\\' && i + 1 < source.size()) display += source[++i];
            else if (c == quote) quote = {};
            continue;
        }
        if (c == '"') {
            if (QStringView(source).mid(i, 3) == u"\"\"\"") return source;
            quote = c;
        } else if (c == '#') { display += source.mid(i); break; }
        else if (QStringView(u"([{").contains(c)) ++depth;
        else if (QStringView(u")]}").contains(c)) --depth;
        else if (c == ':' && depth == 0) return source;
        else if (c == ';' && depth == 0) {
            display += '\n';
            while (i + 1 < source.size() && source[i + 1] == ' ') ++i;
            continue;
        }
        display += c;
    }
    return depth == 0 && quote.isNull() ? display : source;
}
}
