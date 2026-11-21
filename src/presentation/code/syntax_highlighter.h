#pragma once

#include <QString>
#include <QStringView>

namespace exagent {

// 词法着色与视图分离，后续可替换为完整语言语法引擎。
class SyntaxHighlighter {
public:
    struct Result { QString html; int endState = 0; };
    static Result scan(QStringView line, const QString &language, int startState,
                       bool dark, bool produceHtml);
    static QString normalizeLanguage(const QString &language);
};

} // namespace exagent
