#include "syntax_highlighter.h"

#include <QHash>
#include <QSet>

namespace exagent {
namespace {

struct Vocabulary { QSet<QString> keywords; QSet<QString> types; };
// 各语言独立匹配，避免把其他语言的保留字误标为关键字。
const QHash<QString, Vocabulary> vocabularies = {
    {"go", {
        {"break", "default", "func", "interface", "select", "case", "defer", "go", "map", "struct", "chan", "else", "goto", "package", "switch", "const", "fallthrough", "if", "range", "type", "continue", "for", "import", "return", "var", "nil", "true", "false", "iota"},
        {"any", "comparable", "bool", "byte", "complex64", "complex128", "error", "float32", "float64", "int", "int8", "int16", "int32", "int64", "rune", "string", "uint", "uint8", "uint16", "uint32", "uint64", "uintptr"}}},
    {"c", {
        {"auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern", "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof", "static", "struct", "switch", "typedef", "union", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local"},
        {"char", "double", "float", "int", "long", "short", "signed", "unsigned", "void", "_Bool", "_Complex", "_Imaginary", "size_t"}}},
    {"cpp", {
        {"alignas", "alignof", "and", "asm", "auto", "break", "case", "catch", "class", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "or", "override", "private", "protected", "public", "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while"},
        {"bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short", "signed", "unsigned", "void", "wchar_t", "size_t"}}},
    {"python", {
        {"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"},
        {"bool", "bytes", "dict", "float", "int", "list", "object", "set", "str", "tuple"}}},
    {"java", {
        {"abstract", "assert", "break", "case", "catch", "class", "const", "continue", "default", "do", "else", "enum", "extends", "false", "final", "finally", "for", "goto", "if", "implements", "import", "instanceof", "interface", "native", "new", "null", "package", "private", "protected", "public", "record", "return", "static", "strictfp", "super", "switch", "synchronized", "this", "throw", "throws", "transient", "true", "try", "volatile", "while"},
        {"boolean", "byte", "char", "double", "float", "int", "long", "short", "void"}}},
    {"javascript", {
        {"as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally", "for", "from", "function", "get", "if", "import", "in", "instanceof", "let", "new", "null", "of", "return", "set", "static", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield"},
        {}}},
    {"typescript", {
        {"abstract", "any", "declare", "enum", "implements", "infer", "interface", "is", "keyof", "namespace", "never", "private", "protected", "public", "readonly", "satisfies", "type", "unknown", "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally", "for", "from", "function", "get", "if", "import", "in", "instanceof", "let", "new", "null", "of", "return", "set", "static", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield"},
        {"boolean", "bigint", "number", "object", "string", "symbol"}}},
    {"rust", {
        {"as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while"},
        {"bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128", "isize", "str", "u8", "u16", "u32", "u64", "u128", "usize"}}},
    {"json", {
        {"true", "false", "null"},
        {}}},
    {"yaml", {
        {"true", "false", "null"},
        {}}},
    {"qml", {
        {"property", "signal", "readonly", "required", "alias", "component", "default", "on", "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally", "for", "from", "function", "get", "if", "import", "in", "instanceof", "let", "new", "null", "of", "return", "set", "static", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield"},
        {"bool", "color", "date", "double", "int", "list", "real", "string", "url", "var"}}},
    {"powershell", {
        {"begin", "break", "catch", "class", "continue", "data", "do", "dynamicparam", "else", "elseif", "end", "enum", "exit", "filter", "finally", "for", "foreach", "from", "function", "if", "in", "param", "process", "return", "switch", "throw", "trap", "try", "until", "using", "while", "workflow"},
        {}}},
};
const QSet<QString> shellKeywords = {
    "if", "then", "elif", "else", "fi", "for", "in", "do", "done", "while", "until",
    "case", "esac", "function", "select", "return", "break", "continue", "export", "local"
};
const QSet<QString> shellCommands = {
    "set", "cd", "test", "read", "echo", "printf", "source", "exec", "exit", "shift", "trap",
    "go", "git", "curl", "find", "rg", "grep", "sed", "awk", "sort", "cat", "mkdir", "cmake", "python3"
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
    if (name == "golang") return "go";
    if (name == "js" || name == "jsx") return "javascript";
    if (name == "ts" || name == "tsx") return "typescript";
    if (name == "py") return "python";
    if (name == "sh" || name == "zsh" || name == "bash" || name == "ksh" || name == "dash") return "shell";
    if (name == "ps1" || name == "pwsh") return "powershell";
    if (name == "yml") return "yaml";
    return name;
}

SyntaxHighlighter::Result SyntaxHighlighter::scan(QStringView line, const QString &language,
                                                 int startState, bool dark, bool produceHtml)
{
    Result result;
    int state = startState;
    // 编译日志只对常见的回显命令着色，普通输出按原样展示；行索引无需解析日志。
    const bool terminal = language == "terminal-output" || language == "go-read-output";
    const auto trimmed = terminal && produceHtml ? line.trimmed() : QStringView{};
    const bool echoedCommand = terminal && produceHtml
        && (trimmed.startsWith(u"cd ") || trimmed.startsWith(u"git ") || trimmed.startsWith(u"go ")
            || trimmed.startsWith(u"cmake ") || trimmed.startsWith(u"$ "));
    const bool shell = language == "shell" || echoedCommand;
    const bool powershell = language == "powershell";
    const bool hashComments = powershell || language == "python" || shell || language == "yaml";
    const bool slashComments = language == "cpp" || language == "c" || language == "java"
        || language == "javascript" || language == "typescript" || language == "go" || language == "rust"
        || language == "css" || language == "qml";
    const bool python = language == "python";
    const bool templateStrings = language == "go" || language == "qml" || language == "javascript" || language == "typescript";
    const bool sql = language == "sql";
    const bool known = slashComments || hashComments || sql || language == "json";
    // 索引阶段只需行尾状态。普通行没有跨行起始符，可以直接沿用默认状态；
    // 带候选符号或承接上一行的内容仍由完整词法逻辑判断字符串和注释边界。
    if (!produceHtml && startState == 0
        && (!(slashComments || sql) || !line.contains(u"/*"))
        && (!templateStrings || !line.contains(u'`'))
        && (!python || (!line.contains(u"'''") && !line.contains(u"\"\"\""))))
        return result;

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
    // 返回变量名的结束位置；参数展开运算符留给外层扫描，避免吞掉后续命令。
    const auto variableEnd = [&](qsizetype dollar) {
        auto j = dollar + 1;
        const bool braces = j < line.size() && line[j] == '{';
        if (braces) ++j;
        const auto name = j;
        while (j < line.size() && (line[j].isLetterOrNumber() || line[j] == '_')) ++j;
        if (j == name && shell && j < line.size() && QStringView(u"@*#?$!-").contains(line[j])) ++j;
        if (j == name) return dollar + 1;
        if (braces && j < line.size() && line[j] == '}') ++j;
        return j;
    };
    const auto vocabulary = vocabularies.constFind(language);
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
        if ((slashComments && line.mid(i, 2) == "//") || (hashComments && line[i] == '#' && (!shell || i == 0 || line[i-1].isSpace() || QStringView(u";|&()").contains(line[i-1])))
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
            while (i < line.size() && line[i].isSpace()) ++i;
            while (i < line.size() && identifierPart(line[i])) ++i;
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
                    const bool escape = powershell ? quote == '"' && line[i] == '`'
                        : line[i] == '\\' && (!shell || quote != '\'');
                    if (escape) { i = qMin(i + 2, line.size()); continue; }
                    if (line[i++] == quote) break;
                }
                auto after = i;
                while (after < line.size() && line[after].isSpace()) ++after;
                if ((shell || powershell) && quote == '"' && produceHtml) {
                    auto segment = start;
                    for (auto pos = start + 1; pos < i; ++pos) {
                        if ((shell && line[pos] == '\\') || (powershell && line[pos] == '`')) { ++pos; continue; }
                        if (line[pos] != '$') continue;
                        const auto end = variableEnd(pos);
                        if (end == pos + 1) continue;
                        append(segment, pos - segment, stringColor);
                        append(pos, end - pos, typeColor);
                        segment = end; pos = end - 1;
                    }
                    append(segment, i - segment, stringColor);
                } else append(start, i - start, language == "json" && after < line.size() && line[after] == ':' ? typeColor : stringColor);
            }
            continue;
        }
        if ((shell || powershell) && line[i] == '$') {
            i = variableEnd(i);
            append(start, i - start, typeColor);
            continue;
        }
        if ((shell || powershell) && line[i] == '-' && (i == 0 || line[i-1].isSpace())) {
            ++i;
            while (i < line.size() && (line[i].isLetterOrNumber() || line[i] == '-')) ++i;
            append(start, i - start, numberColor);
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
            if (shell && shellKeywords.contains(word)) color = keywordColor;
            else if (shell && shellCommands.contains(word)) color = functionColor;
            else if (shell && i < line.size() && line[i] == '=') color = typeColor;
            else if ((vocabulary != vocabularies.cend() && vocabulary->keywords.contains(powershell ? word.toLower() : word)) || (sql && sqlKeywords.contains(word.toUpper()))) color = keywordColor;
            else if (vocabulary != vocabularies.cend() && vocabulary->types.contains(word)) color = typeColor;
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
