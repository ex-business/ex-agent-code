#include "terminal_preferences.h"
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <yaml-cpp/yaml.h>

namespace exagent {
QVariantMap TerminalPreferences::values() const {
    return {{"shell",m_settings.value("terminal/shell",qEnvironmentVariable("SHELL",QStringLiteral("/bin/sh")))},
            {"arguments",m_settings.value("terminal/arguments",QStringLiteral("-l"))},
            {"commands",m_settings.value("terminal/commands",QStringLiteral("git status\npwd"))},
            {"theme",m_settings.value("terminal/theme").toMap()}};
}
QString TerminalPreferences::save(const QString &shell, const QString &arguments, const QString &commands) {
    const QFileInfo info(shell);
    if (!info.isAbsolute() || !info.isFile() || !info.isExecutable()) return QStringLiteral("请选择可执行的 Shell 绝对路径。");
    if (arguments.size()>8192 || commands.size()>65536 || arguments.contains(QChar::Null) || commands.contains(QChar::Null))
        return QStringLiteral("配置内容过长或包含空字符。");
    if (commands.split('\n').size()>256) return QStringLiteral("快速命令最多 256 条。");
    for (const auto ch:arguments+commands)
        if (ch.category()==QChar::Other_Control && ch!='\n' && ch!='\t') return QStringLiteral("配置不能包含终端控制字符。");
    m_settings.setValue("terminal/shell",shell);
    m_settings.setValue("terminal/arguments",arguments);
    m_settings.setValue("terminal/commands",commands);
    m_settings.sync();
    if (m_settings.status()!=QSettings::NoError) return QStringLiteral("终端设置保存失败。");
    emit changed(); return {};
}
QStringList TerminalPreferences::shellCommand() {
    QSettings settings;
    const auto program=settings.value("terminal/shell").toString();
    if (program.isEmpty()) return {}; // 未配置时沿用 tmux 的平台默认 Shell。
    QStringList result{program};
    for (const auto &arg:settings.value("terminal/arguments").toString().split('\n'))
        if (!arg.isEmpty()) result.append(arg);
    // tmux 只有一个命令参数时交给 Shell 解析，因此单独引用可执行路径。
    if (result.size()==1) {
        auto path=result.first();
        path.replace("'", "'\"'\"'");
        result[0]="'"+path+"'";
    }
    return result;
}
QString TerminalPreferences::importTheme(const QUrl &url, int format) {
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size()>256*1024)
        return QStringLiteral("主题文件不可读取或超过 256 KiB。");
    QVariantMap theme;
    auto color=[&](QString key, QString value) {
        value=value.trimmed();
        if (QRegularExpression("^[0-9a-fA-F]{6}$").match(value).hasMatch()) value.prepend('#');
        const QColor parsed(value);
        if (!parsed.isValid()) throw std::runtime_error("无效颜色");
        theme.insert(key,parsed.name());
    };
    try {
        const auto bytes=file.readAll();
        if (format==0) {
            for (auto line:QString::fromUtf8(bytes).split('\n')) {
                line=line.trimmed();
                if (line.isEmpty() || line.startsWith('#')) continue;
                const auto equal=line.indexOf('=');
                if (equal<0) continue;
                const auto key=line.left(equal).trimmed();
                auto value=line.mid(equal+1).trimmed();
                if (value.startsWith('"') && value.endsWith('"')) value=value.mid(1,value.size()-2);
                if (key=="palette") {
                    const auto split=value.indexOf('='); bool ok=false;
                    const auto index=value.left(split).toInt(&ok,0);
                    if (split<0 || !ok || index<0 || index>255) throw std::runtime_error("调色板索引无效");
                    color("ansi"+QString::number(index),value.mid(split+1));
                } else if (key=="background" || key=="foreground" || key=="cursor-color" || key=="selection-background") color(key,value);
            }
        } else if (format==1) {
            const auto yaml=YAML::Load(bytes.constData());
            for (const auto &key:{"background","foreground","cursor"})
                if (yaml[key]) color(QString(key)=="cursor" ? "cursor-color" : key,QString::fromStdString(yaml[key].as<std::string>()));
            const QStringList names{"black","red","green","yellow","blue","magenta","cyan","white"};
            for (int group=0;group<2;++group) {
                const auto colors=yaml["terminal_colors"] ? yaml["terminal_colors"][group ? "bright" : "normal"] : YAML::Node();
                for (int index=0;index<names.size();++index)
                    if (colors && colors[names[index].toStdString()]) color("ansi"+QString::number(group*8+index),QString::fromStdString(colors[names[index].toStdString()].as<std::string>()));
            }
            if (!theme.contains("cursor-color") && yaml["accent"]) color("cursor-color",QString::fromStdString(yaml["accent"].as<std::string>()));
        } else return QStringLiteral("不支持的主题格式。");
    } catch (const std::exception &) { return QStringLiteral("主题格式或颜色无效，原主题已保留。"); }
    if (!theme.contains("background") || !theme.contains("foreground")) return QStringLiteral("主题必须提供背景色和前景色。");
    // 完整解析后再发布，失败时不留下半套调色板。
    m_settings.setValue("terminal/theme",theme); emit changed(); return {};
}
void TerminalPreferences::resetTheme() { m_settings.remove("terminal/theme"); emit changed(); }
}
