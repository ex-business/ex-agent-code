#include "session_binding.h"
#include "agent/codex/thread_input.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace exagent::cli {
QString executable(int provider) {
  auto name = provider == Pi ? QStringLiteral("pi") : QStringLiteral("claude");
  auto path = QStandardPaths::findExecutable(name);
  if (path.isEmpty())
    path = QStandardPaths::findExecutable(
        name, {"/opt/homebrew/bin", "/usr/local/bin",
               QDir::homePath() + "/.local/bin"});
  return path;
}
QString leaseKey(const QJsonObject &s) {
  return "external-agent:" + QString::number(s["provider"].toInt()) + ":" +
         QFileInfo(s["file"].toString()).canonicalFilePath();
}
bool validSource(const QJsonObject &s, QString *error) {
  const auto provider = s["provider"].toInt();
  const auto id = s["id"].toString(), cwd = s["cwd"].toString(),
             path = s["file"].toString();
  const QFileInfo info(path);
  QFile f(path);
  if ((provider != Pi && provider != Claude) || QUuid(id).isNull() ||
      !QDir::isAbsolutePath(path) || info.isSymLink() || !info.isFile() ||
      info.size() > 32 * 1024 * 1024 || !QFileInfo(cwd).isDir() ||
      QFileInfo(cwd).canonicalFilePath() != cwd ||
      !f.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("Agent历史绑定无效或超过32MiB。");
    return false;
  }
  for (int i = 0; i < 256 && f.pos() < 1024 * 1024 && !f.atEnd(); ++i) {
    const auto row = QJsonDocument::fromJson(f.readLine(256 * 1024)).object();
    if (provider == Pi) {
      if (i == 0 && row["type"] == "session" && row["id"] == id &&
          QFileInfo(row["cwd"].toString()).canonicalFilePath() == cwd)
        return true;
      break;
    }
    if (row["sessionId"] == id &&
        QFileInfo(row["cwd"].toString()).canonicalFilePath() == cwd &&
        !row["isSidechain"].toBool() && info.completeBaseName() == id)
      return true;
  }
  *error = QStringLiteral("Agent历史与会话ID、工作目录不匹配。");
  return false;
}
int terminalContinuation(const QJsonObject &binding) {
  auto source = binding["source"].toObject();
  if (source.isEmpty())
    source = binding;
  QString error;
  if (!validSource(source, &error))
    return 2;
  auto lease = codex::claimThread(leaseKey(source));
  if (!lease) {
    qWarning("此Agent会话正由另一入口占用。");
    return 3;
  }
  const auto program = binding["program"].toString();
  if (!QFileInfo(program).isAbsolute() || !QFileInfo(program).isExecutable())
    return 2;
  QProcess process;
  process.setProgram(program);
  process.setWorkingDirectory(source["cwd"].toString());
  QStringList args;
  const auto native = binding["native"].toObject();
  if (source["provider"] == Pi)
    args = {"--session", source["file"].toString()};
  else
    args = {"--resume", source["id"].toString()};
  if (!native.isEmpty() && !native["write"].toBool())
    args << "--tools"
         << (source["provider"] == Pi ? "read,grep,find,ls" : "Read,Grep,Glob");
  if (source["provider"] == Pi && !native.isEmpty()) {
    const auto guard = installPiGuard();
    if (guard.isEmpty())
      return 2;
    args << "--no-extensions"
         << "--extension" << guard;
  }
  if (source["provider"] == Claude && !native.isEmpty())
    args << "--setting-sources"
         << ""
         << "--strict-mcp-config"
         << "--mcp-config"
         << R"({"mcpServers":{}})";
  if (source["provider"] == Pi) {
    const auto extension = qEnvironmentVariable("EX_PI_TERMINAL_EXTENSION");
    const auto expected =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/terminal-hibernation/pi_hibernation.mjs";
    if (!extension.isEmpty()) {
      if (extension != expected || QFileInfo(extension).isSymLink() ||
          !QFileInfo(extension).isFile())
        return 2;
      args << "--extension" << extension;
    }
  }
  if (!native.value("model").toString().isEmpty())
    args << "--model" << native.value("model").toString();
  if (source["provider"] == Claude && native.value("fullAccess").toBool())
    args << "--dangerously-skip-permissions";
  process.setArguments(args);
#ifdef Q_OS_UNIX
  // 与原生执行共用输入租约；父进程持锁直到实际CLI退出，窗口关闭不抢走输入权。
  QList<QByteArray> values{QFile::encodeName(program)}, environment;
  for (const auto &arg : args)
    values.append(arg.toUtf8());
  auto env = QProcessEnvironment::systemEnvironment();
  if (!native.isEmpty()) {
    env.insert("EX_NATIVE_WRITE", native["write"].toBool() ? "1" : "0");
    env.insert("EX_NATIVE_FULL", native["fullAccess"].toBool() ? "1" : "0");
  }
  env.insert("EX_TERMINAL_OWNER_PID",
             QString::number(QCoreApplication::applicationPid()));
  for (const auto &entry : env.toStringList())
    environment.append(entry.toUtf8());
  QList<char *> av, ev;
  for (auto &v : values)
    av.append(v.data());
  for (auto &v : environment)
    ev.append(v.data());
  av.append(nullptr);
  ev.append(nullptr);
  if (!QDir::setCurrent(source["cwd"].toString()) ||
      fcntl(lease->handle(), F_SETFD, 0) < 0)
    return 2;
  auto pid = fork();
  if (pid < 0)
    return 2;
  if (pid == 0) {
    execve(av.front(), av.data(), ev.data());
    _exit(126);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR)
      return 2;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 7;
#else
  process.setInputChannelMode(QProcess::ForwardedInputChannel);
  process.setProcessChannelMode(QProcess::ForwardedChannels);
  process.start();
  if (!process.waitForStarted() || !process.waitForFinished(-1))
    return 2;
  return process.exitCode();
#endif
}
} // namespace exagent::cli
