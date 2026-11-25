#include "cua_provider.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>

namespace exagent::computer {
QString driverPath()
{
    // 显式配置的路径不可用时直接报错，避免误操作其他提供方。
    const QString configured = qEnvironmentVariable("EX_AGENT_CUA_DRIVER");
    QStringList candidates;
    if (!configured.isEmpty()) {
        candidates << configured;
    } else {
#ifdef Q_OS_MACOS
        const QString binary = QStringLiteral("/CuaDriver.app/Contents/MacOS/cua-driver");
        candidates << QStringLiteral("/Applications") + binary << QDir::homePath() + "/Applications" + binary;
#endif
        candidates << QDir::homePath() + "/.local/bin/cua-driver";
    }
    for (const QString &candidate : candidates) {
        const QFileInfo file(candidate);
        if (file.isAbsolute() && file.isFile() && file.isExecutable()) return file.canonicalFilePath();
    }
    return {};
}

int fail(const QString &code, const QString &message)
{
    const auto data = QJsonDocument(QJsonObject{{"error", code}, {"message", message}}).toJson(QJsonDocument::Compact);
    std::fprintf(stderr, "%s\n", data.constData());
    return 2;
}

int invokeDriver(const QString &program, const QStringList &arguments, const QByteArray &input, bool schemaJson)
{
    QProcess driver;
    driver.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    driver.start(program, arguments);
    if (!driver.waitForStarted(3000)) return fail("provider_start", QStringLiteral("无法启动 CuaDriver。"));
    // 动作参数可能含有输入文本，只走标准输入。保留驱动返回的快照令牌、错误及退出状态。
    if (!input.isEmpty()) driver.write(input);
    driver.closeWriteChannel();
    if (!driver.waitForFinished(45000)) {
        driver.kill();
        driver.waitForFinished(2000);
        return fail("provider_timeout", QStringLiteral("等待 CuaDriver 超时，请先重新读取目标窗口再决定是否重试。"));
    }
    if (driver.exitStatus() != QProcess::NormalExit) return fail("provider_exit", QStringLiteral("CuaDriver 异常结束。"));
    const auto bytes = driver.readAllStandardOutput();
    if (schemaJson && !driver.exitCode()) {
        const QByteArray marker("input_schema:\n");
        const auto at = bytes.lastIndexOf(marker);
        const auto json = at < 0 ? QJsonDocument{} : QJsonDocument::fromJson(bytes.mid(at + marker.size()));
        if (!json.isObject()) return fail("invalid_schema", QStringLiteral("驱动没有返回可识别的动作参数定义。"));
        std::puts(json.toJson(QJsonDocument::Compact).constData());
        return 0;
    }
    if (!bytes.isEmpty()) std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);
    if (driver.exitCode() || arguments.value(0) != "call") return driver.exitCode();
    // 部分驱动错误只写在 JSON 中，仍以非零退出码告知脚本，防止误判动作成功。
    const auto result = QJsonDocument::fromJson(bytes);
    if (!result.isObject()) return fail("invalid_reply", QStringLiteral("驱动未返回可识别的 JSON 结果。"));
    const auto object = result.object();
    if (object.value("status") == QStringLiteral("refused") || object.value("refusal").isObject()
            || (object.contains("code") && !object.contains("effect") && !object.contains("delivery"))
            || object.value("effect") == QStringLiteral("failed")
            || object.value("escalation").toObject().value("reason") == QStringLiteral("delivery_failed")
            || (object.contains("error") && !object.value("error").isNull()) || object.value("isError").toBool()
            || (object.contains("ok") && !object.value("ok").toBool())) return 3;
    return 0;
}
}
