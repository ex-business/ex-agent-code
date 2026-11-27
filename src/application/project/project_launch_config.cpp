#include "project_launch_config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <yaml-cpp/yaml.h>

namespace exagent {
QVariantMap readLaunchDefaults(const QString &root, QString *error)
{
    QVariantMap defaults{{"host","local"}, {"task",""}, {"start","HEAD"}};
    const auto path = QDir(root).filePath("orca.yaml");
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return defaults;
    QFile file(path);
    if (!info.isFile() || info.isSymLink() || !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取项目配置：") + path;
        return {};
    }
    const auto bytes = file.read(262145);
    if (bytes.size() > 262144 || file.error() != QFileDevice::NoError) {
        *error = QStringLiteral("orca.yaml 读取失败或超过 256 KiB。");
        return {};
    }
    try {
        const YAML::Node doc = YAML::Load(bytes.toStdString());
        if (doc.IsNull()) return defaults;
        if (!doc.IsMap()) throw std::runtime_error("root");
        const auto ext = doc["x-ex-agent"];
        if (!ext) return defaults;
        const auto fields = ext["launch"];
        if (!fields) return defaults;
        if (!fields.IsMap()) throw std::runtime_error("launch");
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            const auto value = fields[it.key().toStdString()];
            if (!value) continue;
            if (!value.IsScalar()) throw std::runtime_error("scalar");
            const QString text = QString::fromStdString(value.Scalar()).trimmed();
            if (text.size() > 2048 || text.contains(QChar::Null)) throw std::runtime_error("length");
            if (!text.isEmpty()) it.value() = text;
        }
    } catch (const std::exception &) {
        *error = QStringLiteral("orca.yaml 格式错误，请检查 x-ex-agent.launch 下的 host、task 和 start。");
        return {};
    }
    return defaults;
}

QString writeLaunchTemplate(const QString &root)
{
    QFile file(QDir(root).filePath("orca.yaml"));
    // 只允许显式新建，已有项目文件及其中其他 Orca 配置必须保留。
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return QStringLiteral("无法新建 orca.yaml，文件可能已存在：") + file.errorString();
    const QByteArray text = "x-ex-agent:\n  launch:\n    host: local\n    start: HEAD\n    task: \"\"\n";
    if (file.write(text) != text.size() || !file.flush()) return QStringLiteral("项目配置写入失败。");
    return {};
}
}
