#include "manifest.h"
#include "domain/ipc/protocol.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace exagent::plugins {
QJsonObject validateManifest(const QJsonObject &input, QString *error) {
    const QSet<QString> fields{"apiVersion",  "id",           "name",     "version", "execution",
                               "description", "capabilities", "commands", "panels"};
    const QRegularExpression identifier("^[a-z][a-z0-9-]{0,63}$"),
        version("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})$");
    auto fail = [error](const QString &reason) {
        *error = reason;
        return QJsonObject{};
    };
    for (auto it = input.begin(); it != input.end(); ++it)
        if (!fields.contains(it.key()))
            return fail(QStringLiteral("插件包含当前 API 未定义的字段：") + it.key());
    if (!ipc::integer(input["apiVersion"], 1, 1) || !identifier.match(input["id"].toString()).hasMatch() ||
        input["id"] == "constructor" || input["id"] == "prototype" ||
        !version.match(input["version"].toString()).hasMatch() || input["version"].toString().size() > 40 ||
        input["name"].toString().trimmed().isEmpty() || input["name"].toString().size() > 120 ||
        !ipc::integer(input["execution"], Data, Declarative))
        return fail(QStringLiteral("插件 API、标识、版本、名称或执行级别无效。"));
    if (!input["capabilities"].isArray() || !input["commands"].isArray() || !input["panels"].isArray() ||
        input["capabilities"].toArray().size() > 32 || input["commands"].toArray().size() > 64 ||
        input["panels"].toArray().size() > 16 ||
        (!input["description"].isUndefined() && !input["description"].isString()) ||
        input["description"].toString().size() > 2048)
        return fail(QStringLiteral("插件贡献或能力声明超过支持范围。"));
    QSet<QString> capabilities;
    for (const auto &value : input["capabilities"].toArray()) {
        const auto name = value.toString();
        // 原生能力按方法精确授权，插件管理和自身调用不能递归授权。
        if (name.isEmpty() || name.size() > 100 || name.startsWith("plugins.") || capabilities.contains(name))
            return fail(QStringLiteral("插件能力声明无效或重复。"));
        capabilities.insert(name);
    }
    if (input["execution"].toInt() == Data &&
        (!capabilities.isEmpty() || !input["commands"].toArray().isEmpty()))
        return fail(QStringLiteral("数据级插件不能声明命令或宿主能力。"));
    QSet<QString> ids;
    for (const auto &value : input["commands"].toArray()) {
        const auto command = value.toObject();
        if (command.size() != 4 || !identifier.match(command["id"].toString()).hasMatch() ||
            ids.contains(command["id"].toString()) || command["title"].toString().trimmed().isEmpty() ||
            command["title"].toString().size() > 120 || !command["params"].isObject() ||
            !capabilities.contains(command["method"].toString()))
            return fail(QStringLiteral("插件命令无效、重复或缺少对应能力。"));
        ids.insert(command["id"].toString());
    }
    ids.clear();
    for (const auto &value : input["panels"].toArray()) {
        const auto panel = value.toObject();
        if (panel.size() != 3 || !identifier.match(panel["id"].toString()).hasMatch() ||
            ids.contains(panel["id"].toString()) || panel["title"].toString().trimmed().isEmpty() ||
            panel["title"].toString().size() > 120 || !panel["text"].isString() ||
            panel["text"].toString().size() > 32768)
            return fail(QStringLiteral("插件面板声明无效或超过容量限制。"));
        ids.insert(panel["id"].toString());
    }
    return input;
}
QJsonObject readManifest(const QString &directory, QString *error) {
    const QFileInfo root(directory), info(QDir(directory).filePath("ex-plugin.json"));
    QFile file(info.filePath());
    if (!root.isAbsolute() || !root.isDir() || info.isSymLink() || !info.isFile() ||
        !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("请选择包含 ex-plugin.json 的本地插件目录。");
        return {};
    }
    const auto bytes = file.read(262145);
    if (bytes.size() > 262144 || file.error() != QFileDevice::NoError) {
        *error = QStringLiteral("插件清单读取失败或超过 256 KiB。");
        return {};
    }
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        *error = QStringLiteral("插件清单必须是 JSON 对象。");
        return {};
    }
    return validateManifest(doc.object(), error);
}
} // namespace exagent::plugins
