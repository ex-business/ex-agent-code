#include "window_action.h"
#include <QJsonArray>
#include <QSet>

namespace exagent::computer {
namespace {
bool coordinate(const QJsonValue &v)
{
    return v.isDouble() && v.toDouble() >= 0;
}
bool shortcutAllowed(const QString &command, const QJsonObject &input)
{
    auto parts = input.value(command == "hotkey" ? "keys" : "modifiers").toArray();
    if (command == "press-key") {
        if (input.contains("modifiers") && !input.value("modifiers").isArray()) return false;
        parts.append(input.value("key"));
    }
    QSet<QString> mods;
    QString key;
    for (const auto &part : parts) {
        const auto value = part.toString().trimmed().toLower();
        if (QStringList{"cmd","command","meta","super","win","windows","cmdorctrl","commandorcontrol","⌘"}.contains(value)) mods.insert("cmd");
        else if (QStringList{"ctrl","control","ctl","⌃"}.contains(value)) mods.insert("ctrl");
        else if (QStringList{"alt","option","opt","⌥"}.contains(value)) mods.insert("alt");
        else if (value == "shift" || value == QStringLiteral("⇧")) mods.insert("shift");
        else if (key.isEmpty() && !value.isEmpty()) key = value;
        else return false;
    }
    if (key.isEmpty() || (command == "hotkey" && mods.isEmpty())) return false;
    const bool cmd = mods.contains("cmd"), ctrl = mods.contains("ctrl"), alt = mods.contains("alt");
    // 多种修饰键拼法在驱动中可能等价，先归一检查。拒绝关闭窗口、强退、锁屏和注销。
    const bool simple = key.size() == 1 && (key.at(0).isLetterOrNumber() || QStringLiteral(".,/;-=[]'").contains(key));
    static const QSet<QString> named{"return","enter","tab","space","backspace","delete","forwarddelete","esc","escape",
        "up","down","left","right","home","end","pageup","pagedown","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12"};
    if (!simple && !named.contains(key)) return false;
    if ((cmd || ctrl) && (key == "q" || key == "w")) return false;
    if ((cmd && key == "l") || (alt && key == "f4") || (ctrl && key == "f4")) return false;
    if (cmd && alt && (key == "escape" || key == "esc")) return false;
    return !(ctrl && alt && (key == "delete" || key == "forwarddelete"));
}
}
QString validateWindowAction(const QString &command, const QJsonObject &input)
{
    if (command == "drag") {
        for (const auto &key : {"from_x","from_y","to_x","to_y"})
            if (!coordinate(input.value(key))) return QStringLiteral("拖动需要当前窗口快照中的起点与终点坐标。");
        return {};
    }
    const bool hasIndex = input.contains("element_index");
    const auto index = input.value("element_index");
    if (hasIndex && (!index.isDouble() || index.toDouble() < 0 || double(index.toInteger()) != index.toDouble()
                    || input.value("snapshot_id").toString().isEmpty()))
        return QStringLiteral("元素序号必须与最近一次窗口快照的 snapshot_id 一起使用。");
    const bool element = hasIndex || !input.value("element_token").toString().isEmpty();
    const bool point = coordinate(input.value("x")) && coordinate(input.value("y"));
    if (!element && (command == "set-value" || !point))
        return QStringLiteral("请提供目标窗口的快照元素，或动作支持的完整窗口坐标。");
    if ((command == "hotkey" || command == "press-key") && !shortcutAllowed(command,input))
        return QStringLiteral("不支持此按键或系统级快捷键。");
    return {};
}
}
