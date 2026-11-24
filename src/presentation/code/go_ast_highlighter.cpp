#include "go_ast_highlighter.h"
#include <tree_sitter/api.h>
#include "vscode_token_theme.h"
#include <memory>
#include <QRegularExpression>

extern "C" const TSLanguage *tree_sitter_go();
namespace exagent {
namespace {
QByteArray scopeFor(TSNode node, TSTreeCursor &cursor)
{
    const QByteArrayView kind(ts_node_type(node));
    if (kind == "comment") return "comment.go";
    if (kind == "raw_string_literal" || kind == "interpreted_string_literal" || kind == "rune_literal") return "string.quoted.go";
    if (kind == "int_literal" || kind == "float_literal" || kind == "imaginary_literal") return "constant.numeric.go";
    if (kind == "true" || kind == "false" || kind == "nil" || kind == "iota") return "constant.language.go";
    if (kind == "type_identifier") return "entity.name.type.go";
    if (kind == "identifier" || kind == "field_identifier") {
        const auto parent = ts_node_parent(node);
        if (ts_node_is_null(parent)) return {};
        const QByteArrayView owner(ts_node_type(parent));
        const char *field = ts_tree_cursor_current_field_name(&cursor);
        const QByteArrayView role(field ? field : "");
        if ((owner == "function_declaration" || owner == "method_declaration") && role == "name") return "entity.name.function.go";
        if (owner == "call_expression" && role == "function") return "entity.name.function.go";
        if ((owner == "parameter_declaration" || owner == "variadic_parameter_declaration") && role == "name") return "variable.parameter.go";
        if (kind == "field_identifier") {
            const auto ancestor = ts_node_parent(parent);
            if (owner == "selector_expression" && !ts_node_is_null(ancestor) &&
                ts_node_eq(parent, ts_node_child_by_field_name(ancestor, "function", 8))) return "entity.name.function.go";
            return "variable.other.property.go";
        }
    }
    if (!ts_node_is_named(node) && !kind.isEmpty() && kind.front() >= 'a' && kind.front() <= 'z') return "keyword.go";
    return {};
}
}
bool highlightGoReadOutput(const QString &code, QVector<CodeColorSpan> &spans, const std::atomic_bool &cancel)
{
    // rg/nl 的位置前缀只参与显示。替换为等长空格后解析，原文、选区和 UTF-16 坐标保持一致。
    static const QRegularExpression prefix(QStringLiteral(R"(^(?:[^:\r\n]+\.go:[0-9]+:(?![0-9]+:)|[ ]*[0-9]+\t))"));
    QString projected;
    QVector<CodeColorSpan> prefixes;
    for (qsizetype start = 0; start < code.size();) {
        if (cancel.load()) return false;
        auto end = code.indexOf('\n', start);
        if (end < 0) end = code.size();
        const auto line = QStringView(code).mid(start, end - start);
        const auto match = prefix.matchView(line);
        if (match.hasMatch()) {
            if (projected.isEmpty()) projected = code;
            prefixes.append({int(start), int(match.capturedLength()), qRgb(100, 104, 111), qRgb(155, 160, 167)});
            for (qsizetype i = 0; i < match.capturedLength(); ++i) projected[start+i] = u' ';
        }
        start = end + 1;
    }
    if (projected.isEmpty()) return false;
    GoParseSession session;
    QVector<CodeColorSpan> syntax;
    if (!highlightGoDocument(session, projected, syntax, cancel)) return false;
    // 前缀在字符串或注释跨行时仍使用次要色，拆分相交的语法区间以保留原始坐标。
    qsizetype next = 0;
    for (auto token : syntax) {
        const int end = token.offset + token.length;
        while (next < prefixes.size() && prefixes[next].offset < end) {
            const auto prefix = prefixes[next++];
            if (prefix.offset > token.offset)
                spans.append({token.offset, prefix.offset - token.offset, token.light, token.dark});
            spans.append(prefix);
            token.offset = qMax(token.offset, prefix.offset + prefix.length);
        }
        if (token.offset < end) spans.append({token.offset, end - token.offset, token.light, token.dark});
    }
    while (next < prefixes.size()) spans.append(prefixes[next++]);
    return true;
}
bool highlightGoDocument(GoParseSession &session, const QString &code, QVector<CodeColorSpan> &spans, const std::atomic_bool &cancel, bool terminalOutput)
{
    if (code.size() > INT_MAX / 2 || cancel.load()) return false;
    // 终端输出来源可能混杂。文件头和函数签名都可作为源码起点，覆盖 sed 截取的文件中段。
    if (terminalOutput) {
        static const QRegularExpression sourceStart(QStringLiteral(R"((?m)^(?:func[ \t]+(?:\([^\r\n)]*\)[ \t]*)?[\p{L}_][\p{L}\p{N}_]*[ \t]*(?:\[[^\n\]]+\])?[ \t]*\(|package[ \t]+[\p{L}_][\p{L}\p{N}_]*[ \t\r]*(?://[^\n]*)?$))"));
        const auto start = sourceStart.matchView(QStringView(code).left(4096));
        if (!start.hasMatch()) return false;
        if (start.capturedView().startsWith(u"func")) {
            // sed 的多个片段可能拼成一棵错误树。分段解析函数，即使末尾被截断也能给已有内容着色。
            auto match = start;
            while (match.hasMatch() && !cancel.load()) {
                const auto from = match.capturedStart();
                auto to = code.indexOf('\n', from);
                if (to < 0) to = code.size();
                while (to < code.size()) {
                    const auto lineStart = to + 1;
                    auto lineEnd = code.indexOf('\n', lineStart);
                    if (lineEnd < 0) lineEnd = code.size();
                    const auto line = QStringView(code).mid(lineStart, lineEnd - lineStart);
                    if (!line.isEmpty() && !line.front().isSpace() && !line.startsWith(u"//") && !line.startsWith(u"}")) break;
                    to = lineEnd;
                    if (line.startsWith(u"}")) break;
                }
                GoParseSession fragment;
                QVector<CodeColorSpan> local;
                highlightGoDocument(fragment, code.mid(from, to-from), local, cancel);
                for (auto span : local) { span.offset += int(from); spans.append(span); }
                match = sourceStart.match(code, to);
                if (match.hasMatch() && !match.capturedView().startsWith(u"func")) break;
            }
            return !spans.isEmpty();
        }
    }
    std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_go())) return false;
#if TREE_SITTER_LANGUAGE_VERSION >= 15
    constexpr auto encoding = Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? TSInputEncodingUTF16LE : TSInputEncodingUTF16BE;
#else
    constexpr auto encoding = TSInputEncodingUTF16;
#endif
    // 流式回答通常只在末尾追加。复用旧树，并按 UTF-16 字节坐标告知解析器编辑范围。
    const bool append = session.tree && code.size() >= session.source.size() && code.startsWith(session.source);
    if (append && code.size() != session.source.size()) {
        const auto start = ts_node_end_point(ts_tree_root_node(session.tree.get()));
        auto end = start;
        for (const auto c : QStringView(code).mid(session.source.size())) {
            if (c == '\n') { ++end.row; end.column = 0; }
            else end.column += 2;
        }
        const auto oldBytes = uint32_t(session.source.size() * 2);
        const TSInputEdit edit{oldBytes, oldBytes, uint32_t(code.size()*2), start, start, end};
        ts_tree_edit(session.tree.get(), &edit);
    }
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(ts_parser_parse_string_encoding(
        parser.get(), append ? session.tree.get() : nullptr, reinterpret_cast<const char *>(code.utf16()), uint32_t(code.size()*2), encoding), ts_tree_delete);
    if (!tree) { session.tree.reset(); session.source.clear(); return false; }
    session.source = code;
    session.tree = std::move(tree);
    // 单次遍历生成有序、不重叠的区间；注释和字符串整体消费，保留原文坐标。
    const auto root = ts_tree_root_node(session.tree.get());
    auto collect = [&](TSNode top) {
        auto cursor = ts_tree_cursor_new(top);
        bool finished = false;
        while (!finished && !cancel.load()) {
            const auto node = ts_tree_cursor_current_node(&cursor);
            const auto scope = scopeFor(node, cursor);
            if (!scope.isEmpty()) {
                const auto start = ts_node_start_byte(node)/2, end = ts_node_end_byte(node)/2;
                if (end > start) {
                    const auto colors = tokenColors(scope);
                    spans.append({int(start), int(end-start), colors.light, colors.dark});
                }
            } else if (ts_tree_cursor_goto_first_child(&cursor)) continue;
            while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
                if (!ts_tree_cursor_goto_parent(&cursor)) { finished = true; break; }
            }
        }
        ts_tree_cursor_delete(&cursor);
    };
    if (terminalOutput) {
        bool inSource = false;
        for (uint32_t i = 0, n = ts_node_named_child_count(root); i < n && !cancel.load(); ++i) {
            const auto node = ts_node_named_child(root, i);
            const QByteArrayView kind(ts_node_type(node));
            const bool function = kind == "function_declaration" || kind == "method_declaration";
            if (kind == "package_clause" || function) inSource = true;
            if ((!function && ts_node_has_error(node)) || !(kind == "comment" || kind == "package_clause" || kind.endsWith("_declaration"))) {
                inSource = false;
                continue;
            }
            if (inSource) collect(node);
        }
    } else collect(root);
    return !cancel.load();
}
}
