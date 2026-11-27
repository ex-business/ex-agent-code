#include "control_host.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace exagent::control {
namespace {
const QMap<QString, QString> Builtins{
    {"ex-cli", QStringLiteral("先用 control capabilities 获取当前桌面命令。projects.list、workspaces.list 和 sessions.list 返回桌面使用的对象 ID。所有写操作使用这些 ID。sessions.send 发送独立输入并继承目标会话已有权限。自动化默认关闭，设置 enabled 为 1 后按 intervalSeconds 周期运行。CLI 输出 JSON，连接中断时核对对象状态后再决定是否重试。")},
    {"ex-checkpoint", QStringLiteral("先读取 workspaces.list 中的 checkpoint。checkpoint 的 state 为整数，0 待办、1 进行中、2 待审查、3 完成。使用 workspaces.checkpoint 传入 project、workspace、comment、state 和读取时的 revision，保存简短进度及下一步。版本变化时重新读取并保留仍然有效的用户说明。")}
};
QString workspaceRoot(Host &host, const QJsonObject &p)
{
    const auto *workspace = host.chat.projects()->catalog.workspace(p["project"].toString(), p["workspace"].toString());
    return workspace ? workspace->path : QString{};
}
QString skillBody(const QString &root, const QString &skill, QString *error)
{
    if (Builtins.contains(skill)) return Builtins.value(skill);
    static const QRegularExpression name("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$");
    if (root.isEmpty() || !name.match(skill).hasMatch()) { *error = QStringLiteral("技能或工作区无效。"); return {}; }
    const auto base = QFileInfo(QDir(root).filePath(".agents/skills")).canonicalFilePath();
    const QFileInfo info(QDir(base).filePath(skill + "/SKILL.md"));
    if (base.isEmpty() || !info.isFile() || info.isSymLink() || !info.canonicalFilePath().startsWith(base + '/')) {
        *error = QStringLiteral("工作区技能不存在或路径超出技能目录。"); return {};
    }
    QFile file(info.canonicalFilePath());
    if (info.size() > 65536 || !file.open(QIODevice::ReadOnly)) { *error = QStringLiteral("技能正文不可读取或超过 64 KiB。"); return {}; }
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError) { *error = QStringLiteral("技能读取失败。"); return {}; }
    return QString::fromUtf8(bytes);
}
}
void registerSkills(Host &host)
{
    host.add("skills.list", {"project", "workspace"}, [&host](const QJsonObject &p, QString *error) {
        QJsonArray items;
        for (auto it = Builtins.begin(); it != Builtins.end(); ++it) items.append(QJsonObject{{"id", it.key()}, {"source", 1}});
        const auto root = workspaceRoot(host, p);
        if ((!p["project"].isUndefined() || !p["workspace"].isUndefined()) && root.isEmpty()) {
            *error = QStringLiteral("工作区不存在。"); return QJsonObject{};
        }
        const auto base = QDir(root).filePath(".agents/skills");
        if (!root.isEmpty()) {
            const auto dirs = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const auto &name : dirs) {
                if (items.size() >= 100) break;
                QString ignored;
                if (!Builtins.contains(name) && !skillBody(root, name, &ignored).isEmpty()) items.append(QJsonObject{{"id", name}, {"source", 2}});
            }
        }
        return QJsonObject{{"items", items}, {"executable", QCoreApplication::applicationFilePath()}};
    });
    host.add("skills.get", {"project", "workspace", "skill"}, [&host](const QJsonObject &p, QString *error) {
        const auto body = skillBody(workspaceRoot(host, p), p["skill"].toString(), error);
        return QJsonObject{{"id", p["skill"]}, {"text", body}};
    });
    host.add("skills.run", {"session", "skill", "request"}, [&host](const QJsonObject &p, QString *error) {
        QString root;
        bool found = false;
        for (const auto &v : host.chat.projects()->rows()) {
            const auto row = v.toMap();
            if (row.value("kind").toInt() != ProjectNavigator::SessionRow || row.value("session").toString() != p["session"].toString()) continue;
            found = true;
            const auto *workspace = host.chat.projects()->catalog.workspace(row.value("project").toString(), row.value("workspace").toString());
            if (workspace) root = workspace->path;
        }
        if (!found || p["request"].toString().trimmed().isEmpty()) { *error = QStringLiteral("请指定目标会话和技能任务。"); return QJsonObject{}; }
        const auto body = skillBody(root, p["skill"].toString(), error);
        if (!error->isEmpty()) return QJsonObject{};
        const auto prompt = QStringLiteral("用户选择的技能 %1\n%2\n\n用户任务\n%3\n\nEX CLI 路径\n%4")
            .arg(p["skill"].toString(), body, p["request"].toString(), QCoreApplication::applicationFilePath());
        if (prompt.size() > 65536) { *error = QStringLiteral("技能与任务的合计正文超过 64K 字符。"); return QJsonObject{}; }
        const auto run = host.chat.agent()->submitIndependent(p["session"].toString(), prompt);
        if (run.isEmpty()) { *error = QStringLiteral("技能派发失败，请检查目标会话权限、账号和活动任务。"); return QJsonObject{}; }
        return QJsonObject{{"run", run}, {"session", p["session"]}, {"skill", p["skill"]}, {"state", 0}};
    });
}
}
