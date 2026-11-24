#include "vscode_token_theme.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
namespace exagent {
namespace {
using Colors = QHash<QByteArray, QRgb>;

// JSONC 的注释与尾逗号仅在字符串之外处理，保留颜色值和转义路径。
QByteArray themeJson(QByteArray data)
{
    bool quoted = false;
    qsizetype comma = -1;
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (quoted) {
            if (c == '\\') ++i;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '/' && i + 1 < data.size() && (data[i+1] == '/' || data[i+1] == '*')) {
            const bool block = data[i+1] == '*';
            auto end = block ? data.indexOf("*/", i+2) : data.indexOf('\n', i+2);
            if (end < 0 && block) return {};
            if (end < 0) end = data.size();
            else if (block) end += 2;
            while (i < end) data[i++] = ' ';
            --i;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if ((c == '}' || c == ']') && comma >= 0) data[comma] = ' ';
        comma = c == ',' ? i : -1;
        quoted = c == '"';
    }
    return data;
}
// 使用 VS Code tokenColors 的 scope 和 foreground，细分 scope 优先于其父级。
void loadTheme(Colors &colors, const QString &path, int depth = 0)
{
    QFile file(path);
    if (depth > 12 || !file.open(QIODevice::ReadOnly)) return;
    const auto object = QJsonDocument::fromJson(themeJson(file.readAll())).object();
    const auto parent = object.value("include").toString();
    if (!parent.isEmpty()) loadTheme(colors, QFileInfo(path).dir().filePath(parent), depth + 1);
    const auto rules = object.value("tokenColors").toArray();
    for (const auto &rule : rules) {
        const auto obj = rule.toObject();
        const auto foreground = obj.value("settings").toObject().value("foreground").toString();
        const QColor color(foreground.size() == 9 ? foreground.left(7) : foreground);
        if (!color.isValid()) continue;
        const auto selector = obj.value("scope");
        QStringList scopes;
        if (selector.isString()) scopes = selector.toString().split(',');
        else for (const auto &scope : selector.toArray()) scopes.append(scope.toString().split(','));
        for (const auto &scope : scopes) colors.insert(scope.trimmed().toLatin1(), color.rgb());
    }
}
Colors theme(const QString &name, const char *env)
{
    Colors colors;
    const auto path = qEnvironmentVariable(env);
    if (!path.isEmpty()) loadTheme(colors, path);
    if (colors.isEmpty()) loadTheme(colors, ":/code-themes/" + name + ".json");
    return colors;
}
QRgb resolve(const Colors &colors, QByteArray scope, QRgb fallback)
{
    while (!scope.isEmpty()) {
        const auto found = colors.constFind(scope);
        if (found != colors.cend()) return *found;
        const auto dot = scope.lastIndexOf('.');
        if (dot < 0) break;
        scope.truncate(dot);
    }
    return fallback;
}
}
TokenColors tokenColors(const QByteArray &scope)
{
    static const Colors light = theme("light", "EX_AGENT_VSCODE_LIGHT_THEME");
    static const Colors dark = theme("dark", "EX_AGENT_VSCODE_DARK_THEME");
    // 主题在进程内固定，避免每个语法节点重复匹配父级 scope。
    static thread_local QHash<QByteArray, TokenColors> cache;
    auto it = cache.constFind(scope);
    if (it != cache.cend()) return *it;
    const TokenColors result{resolve(light, scope, qRgb(35,35,35)), resolve(dark, scope, qRgb(220,220,220))};
    cache.insert(scope, result);
    return result;
}
}
