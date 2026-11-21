#include "syntax_highlighter.h"

#include <QSet>

namespace exagent {
namespace {

const QSet<QString> keywords = {
    "alignas", "alignof", "as", "async", "await", "auto", "assert", "break", "case", "catch",
    "class", "concept", "const", "constexpr", "consteval", "continue", "co_await", "co_return",
    "def", "default", "del", "delete", "do", "elif", "else", "enum", "except", "export", "extends",
    "extern", "false", "False", "final", "finally", "fn", "for", "from", "func", "function",
    "go", "if", "impl", "import", "in", "inline", "instanceof", "interface", "is", "lambda", "let",
    "match", "mutable", "namespace", "new", "nil", "None", "noexcept", "not", "null", "nullptr",
    "operator", "or", "override", "package", "pass", "private", "protected", "public", "raise",
    "range", "requires", "return", "select", "self", "sizeof", "static", "struct", "super",
    "switch", "template", "this", "throw", "throws", "trait", "true", "True", "try", "type",
    "typedef", "typename", "union", "unsafe", "use", "using", "var", "virtual", "volatile",
    "when", "while", "with", "yield", "and"
};
const QSet<QString> types = {
    "bool", "boolean", "byte", "char", "double", "float", "int", "int32", "int64", "long",
    "number", "object", "short", "signed", "size_t", "string", "str", "unsigned", "void"
};
const QSet<QString> sqlKeywords = {
    "SELECT", "FROM", "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "ON", "AS", "AND",
    "OR", "NOT", "NULL", "INSERT", "UPDATE", "DELETE", "INTO", "VALUES", "SET", "ORDER", "BY",
    "GROUP", "LIMIT", "OFFSET", "HAVING", "DISTINCT", "CREATE", "TABLE", "INDEX", "WITH", "ASC", "DESC"
};

bool identifierStart(QChar c) { return c.isLetter() || c == '_' || c == '$'; }
bool identifierPart(QChar c) { return identifierStart(c) || c.isDigit(); }

} // namespace

QString SyntaxHighlighter::normalizeLanguage(const QString &language)
{
    auto name = language.trimmed().toLower();
    if (name == "c++" || name == "cxx" || name == "cc" || name == "hpp" || name == "h") return "cpp";
    if (name == "js" || name == "jsx") return "javascript";
    if (name == "ts" || name == "tsx") return "typescript";
    if (name == "py") return "python";
    if (name == "sh" || name == "zsh" || name == "bash") return "shell";
    if (name == "yml") return "yaml";
    return name;
}

SyntaxHighlighter::Result SyntaxHighlighter::scan(QStringView line, const QString &language,
                                                 int startState, bool dark, bool produceHtml)
{
    Result result;
    int state = startState;
    const bool hashComments = language == "python" || language == "shell" || language == "yaml";
    const bool slashComments = language == "cpp" || language == "c" || language == "java"
        || language == "javascript" || language == "typescript" || language == "go" || language == "rust"
        || language == "css" || language == "qml";
    const bool python = language == "python";
    const bool templateStrings = language == "javascript" || language == "typescript";
    const bool sql = language == "sql";
    const bool known = slashComments || hashComments || sql || language == "json";
    const QString keywordColor = dark ? "#c792ea" : "#8250df";
    const QString stringColor = dark ? "#a8d1a0" : "#28733e";
    const QString numberColor = dark ? "#f2ae75" : "#a34b00";
    const QString typeColor = dark ? "#82c5e8" : "#0969a8";
    const QString functionColor = dark ? "#e9ce8b" : "#795e26";
    const QString commentColor = dark ? "#7f8b97" : "#6b7280";
    auto append = [&](qsizetype start, qsizetype length, const QString &color = QString()) {
        if (!produceHtml || length <= 0) return;
        auto escaped = line.mid(start, length).toString().toHtmlEscaped();
        escaped.replace('\t', "    ");
        escaped.replace(' ', "&nbsp;");
        result.html += color.isEmpty() ? escaped : "<font color=\"" + color + "\">" + escaped + "</font>";
    };
    if (!known) {
        append(0, line.size());
        return result;
    }
    qsizetype i = 0;
    while (i < line.size()) {
        const auto start = i;
        if (state != 0) {
            const QString delimiter = state == 1 ? "*/" : state == 2 ? "'''" : state == 3 ? "\"\"\"" : "`";
            const auto end = line.indexOf(delimiter, i);
            i = end < 0 ? line.size() : end + delimiter.size();
            append(start, i - start, state == 1 ? commentColor : stringColor);
            if (end >= 0) state = 0;
            continue;
        }
        if ((slashComments && line.mid(i, 2) == "//") || (hashComments && line[i] == '#')
            || (sql && line.mid(i, 2) == "--")) {
            append(i, line.size() - i, commentColor);
            break;
        }
        if ((slashComments || sql) && line.mid(i, 2) == "/*") {
            state = 1;
            i += 2;
            const auto end = line.indexOf(u"*/", i);
            i = end < 0 ? line.size() : end + 2;
            append(start, i - start, commentColor);
            if (end >= 0) state = 0;
            continue;
        }
        if ((language == "cpp" || language == "c") && line[i] == '#') {
            ++i;
            while (i < line.size() && (line[i].isLetter() || line[i].isSpace())) ++i;
            append(start, i - start, keywordColor);
            continue;
        }
        if (line[i] == '\'' || line[i] == '"' || (templateStrings && line[i] == '`')) {
            const auto quote = line[i];
            const bool triple = python && line.mid(i, 3) == QString(3, quote);
            if (triple || quote == '`') {
                const QString delimiter = triple ? QString(3, quote) : QString(quote);
                state = triple ? (quote == '\'' ? 2 : 3) : 4;
                i += delimiter.size();
                const auto end = line.indexOf(delimiter, i);
                i = end < 0 ? line.size() : end + delimiter.size();
                append(start, i - start, stringColor);
                if (end >= 0) state = 0;
            } else {
                ++i;
                while (i < line.size()) {
                    if (line[i] == '\\') { i = qMin(i + 2, line.size()); continue; }
                    if (line[i++] == quote) break;
                }
                auto after = i;
                while (after < line.size() && line[after].isSpace()) ++after;
                append(start, i - start, language == "json" && after < line.size() && line[after] == ':' ? typeColor : stringColor);
            }
            continue;
        }
        if (line[i].isDigit()) {
            ++i;
            while (i < line.size() && (line[i].isLetterOrNumber() || line[i] == '.' || line[i] == '_')) ++i;
            append(start, i - start, numberColor);
            continue;
        }
        if (identifierStart(line[i])) {
            ++i;
            while (i < line.size() && identifierPart(line[i])) ++i;
            if (!produceHtml) continue;
            const auto word = line.mid(start, i - start).toString();
            auto after = i;
            while (after < line.size() && line[after].isSpace()) ++after;
            QString color;
            if (keywords.contains(word) || (sql && sqlKeywords.contains(word.toUpper()))) color = keywordColor;
            else if (types.contains(word) || word.front().isUpper()) color = typeColor;
            else if (after < line.size() && line[after] == '(') color = functionColor;
            append(start, i - start, color);
            continue;
        }
        ++i;
        append(start, 1);
    }
    result.endState = state;
    return result;
}

} // namespace exagent
