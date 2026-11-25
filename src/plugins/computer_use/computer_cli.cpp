#include "computer_cli.h"
#include "cua_provider.h"
#include "window_action.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <cstdio>

namespace exagent::computer {
namespace {
const QMap<QString, QString> tools{
    {"permissions", "check_permissions"}, {"list-apps", "list_apps"}, {"list-windows", "list_windows"},
    {"get-app-state", "get_window_state"}, {"click", "click"}, {"set-value", "set_value"},
    {"type-text", "type_text"}, {"press-key", "press_key"}, {"hotkey", "hotkey"},
    {"scroll", "scroll"}, {"drag", "drag"}
};

bool positiveInteger(const QJsonValue &value)
{
    return value.isDouble() && value.toInteger() > 0 && double(value.toInteger()) == value.toDouble();
}

QString validate(const QString &command, QJsonObject &input)
{
    if (command == "list-apps" || command == "permissions")
        return input.isEmpty() ? QString{} : QStringLiteral("此命令无需参数。");
    if (!positiveInteger(input.value("pid"))) return QStringLiteral("请选择 list-apps 返回的目标 pid。");
    if (command == "list-windows") return {};
    if (!positiveInteger(input.value("window_id"))) return QStringLiteral("请选择 list-windows 返回的目标 window_id。");
    // 工作台命令限定到用户选择的本机窗口；驱动自身继续检查权限、快照和目标的有效性。
    for (const auto &key : {"scope", "target", "delivery_mode", "debug_image_out"})
        if (input.contains(key)) return QStringLiteral("不支持覆盖目标、输入投递模式或调试输出。");
    if (command == "get-app-state") {
        if (input.contains("screenshot_out_file")) {
            const QFileInfo out(input.value("screenshot_out_file").toString());
            if (!out.isAbsolute() || out.exists() || out.isSymLink() || !out.dir().exists())
                return QStringLiteral("截图请指定现有目录中尚未使用的绝对文件路径。");
        } else {
            // 默认只取辅助功能树；像素截图需显式提供文件位置。
            input.insert("include_screenshot", false);
        }
        return {};
    }
    return validateWindowAction(command,input);
}
}

int runCli(QStringList arguments)
{
    const bool json = arguments.removeAll("--json") > 0;
    const bool foreground = arguments.removeAll("--foreground") > 0;
    const QString command = arguments.isEmpty() ? QStringLiteral("help") : arguments.takeFirst();
    if (command == "help" || command == "--help") {
        std::puts("ex-agent-code computer <command> [JSON | --stdin] [--json]\n"
                  "  capabilities | permissions | list-apps | list-windows | get-app-state\n"
                  "  click | set-value | type-text | press-key | hotkey | scroll | drag\n"
                  "  describe <command> [--json]         显示驱动的参数定义\n"
                  "  list-windows '{\"pid\":123}'\n"
                  "  get-app-state '{\"pid\":123,\"window_id\":456}'\n"
                  "  set-value --stdin                   从标准输入读取 JSON\n"
                  "  --foreground                        明确允许本次动作在目标窗口前台投递\n"
                  "  先读窗口快照，再使用 element_token 或 snapshot_id 与 element_index 操作。");
        return 0;
    }
    static const QStringList foregroundTools{"click","type-text","press-key","hotkey","scroll","drag"};
    if (foreground && !foregroundTools.contains(command))
        return fail("invalid_arguments", QStringLiteral("此动作不支持前台投递。"));
    const auto path = driverPath();
    if (command == "capabilities") {
        if (!arguments.isEmpty()) return fail("invalid_arguments", QStringLiteral("capabilities 无需参数。"));
        const QJsonObject result{{"provider", "cua-driver"}, {"installed", !path.isEmpty()},
            {"commands", QJsonArray::fromStringList(tools.keys())}};
        std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return 0;
    }
    const bool describe = command == "describe";
    if (describe && arguments.size() != 1) return fail("invalid_arguments", QStringLiteral("describe 需要一个动作名称。"));
    const auto name = tools.value(describe ? arguments.first() : command);
    if (name.isEmpty()) return fail("unknown_command", QStringLiteral("未知动作，请运行 computer help。"));
    if (path.isEmpty()) return fail("provider_missing", QStringLiteral("请安装 CuaDriver，或通过 EX_AGENT_CUA_DRIVER 指定其可执行文件的绝对路径。"));
    if (describe) return invokeDriver(path, {"describe", name}, {}, json);
    QByteArray raw("{}");
    if (arguments.size() > 1) return fail("invalid_arguments", QStringLiteral("请传入单个 JSON 对象或使用 --stdin。"));
    if (arguments.value(0) == "--stdin") {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) return fail("stdin_error", QStringLiteral("无法读取标准输入。"));
        raw.clear();
        while (raw.size() < 65537) {
            const auto chunk = input.read(65537 - raw.size());
            if (chunk.isEmpty()) break;
            raw += chunk;
        }
    } else if (!arguments.isEmpty()) {
        raw = arguments.first().toUtf8();
    }
    if (raw.size() > 65536) return fail("input_too_large", QStringLiteral("单次动作参数不能超过 64 KiB。"));
    const auto document = QJsonDocument::fromJson(raw);
    if (!document.isObject()) return fail("invalid_json", QStringLiteral("动作参数需要一个 JSON 对象。"));
    auto input = document.object();
    const auto error = validate(command, input);
    if (!error.isEmpty()) return fail("invalid_target", error);
    // 只有此次命令显式选择前台，才传给驱动；后台拒绝时不自动重放动作。
    if (foreground) input.insert("delivery_mode","foreground");
    const auto payload = QJsonDocument(input).toJson(QJsonDocument::Compact);
    if (command == "get-app-state" && input.contains("screenshot_out_file")) {
        // 独占创建目标，两个并发的 EX 请求不能写入同一张截图。
        QFile destination(input.value("screenshot_out_file").toString());
        if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            return fail("screenshot_exists", QStringLiteral("无法独占创建截图文件。"));
        const int status = invokeDriver(path, {"call", name}, payload);
        if (status || QFileInfo(destination.fileName()).size() == 0) {
            destination.remove();
            return status ? status : fail("screenshot_missing", QStringLiteral("驱动未生成截图。"));
        }
        return status;
    }
    return invokeDriver(path, {"call", name}, payload);
}
}
