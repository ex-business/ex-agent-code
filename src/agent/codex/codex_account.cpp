#include "codex_account.h"
#include <QDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QUuid>
#include <QFileInfo>
#include <QStandardPaths>
namespace exagent::codex {
QString executable()
{
    auto path = QStandardPaths::findExecutable("codex");
#ifdef Q_OS_MACOS
    if (path.isEmpty()) {
        for (const QString &candidate : { QStringLiteral("/Applications/Codex.app/Contents/Resources/codex"),
                 QStringLiteral(
                     "/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex"),
                 QStringLiteral("/opt/homebrew/bin/codex"), QStringLiteral("/usr/local/bin/codex") }) {
            if (QFileInfo(candidate).isExecutable())
                return candidate;
        }
    }
#endif
    return path;
}
QString identity(Account profile, const QJsonObject &account)
{
    // 只比较账号的公开元数据或专用登录代次，绝不读取或散列密钥与 OAuth token。
    QString source;
    if (profile == Dedicated) {
        source = QSettings("EX Business", "EX Agent Code").value("agent/openaiLoginGeneration").toString();
    } else if (account["type"] == "chatgpt") {
        source = account["email"].toString();
    } else if (account["type"] == "apiKey" && !qEnvironmentVariableIsSet("OPENAI_API_KEY")) {
        const auto home = qEnvironmentVariable("CODEX_HOME", QDir::home().filePath(".codex"));
        const QFileInfo auth(QDir(home).filePath("auth.json"));
        if (auth.isFile())
            source = auth.canonicalFilePath() + QString::number(auth.lastModified().toMSecsSinceEpoch());
    }
    if (source.isEmpty())
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
}
void noteDedicatedLogin()
{
    QSettings("EX Business", "EX Agent Code")
        .setValue("agent/openaiLoginGeneration", QUuid::createUuid().toString());
}
QStringList accountArguments(Account account)
{
    return account == Dedicated
        ? QStringList { "-c", "cli_auth_credentials_store=\"keyring\"", "-c", "model_provider=\"openai\"" }
        : QStringList {};
}
bool prepare(QProcess &process, Account account, QString *error, const QString &profileHome)
{
    const auto path = executable();
    if (path.isEmpty()) {
        *error = QStringLiteral("未找到本机 Codex CLI。");
        return false;
    }
    auto env = QProcessEnvironment::systemEnvironment();
    if(!profileHome.isEmpty()) {
        const auto prefix=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/account-profiles/";
        const auto relative=profileHome.mid(prefix.size());
        if(account!=System || !profileHome.startsWith(prefix) || relative.split('/').size()!=2 || relative.section('/',1)!="codex" || QUuid(relative.section('/',0,0)).isNull()) {
            *error=QStringLiteral("独立账号目录无效。");return false;
        }
        // 独立账号不继承其他账号的环境凭据；目录身份固定在应用私有根下。
        QString path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        for(const auto &part:QString("account-profiles/"+relative).split('/')) {
            path=QDir(path).filePath(part);
            if(QFileInfo(path).isSymLink() || !QDir().mkpath(path) || !QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)) {*error=QStringLiteral("无法准备独立账号私有目录。");return false;}
        }
        for(const auto &key:env.keys())if(key.startsWith("OPENAI_")||key.startsWith("CODEX_"))env.remove(key);
        env.insert("CODEX_HOME",profileHome);
    }
    if (account == Dedicated) {
        // 专用登录和运行共享同一私有目录；Finder 与终端启动均不继承其他 OpenAI 凭据。
        const auto home
            = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/agent-accounts/openai";
        if (!QDir().mkpath(home)
            || !QFile::setPermissions(
                home, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            *error = QStringLiteral("无法创建专用账号目录。");
            return false;
        }
        for (const auto &key : env.keys())
            if (key.startsWith("OPENAI_") || key.startsWith("CODEX_"))
                env.remove(key);
        env.insert("OPENAI_BASE_URL", "https://api.openai.com/v1");
        env.insert("CODEX_HOME", home);
    }
    process.setProgram(path);
    process.setProcessEnvironment(env);
    process.setStandardErrorFile(QProcess::nullDevice());
    return true;
}
}
