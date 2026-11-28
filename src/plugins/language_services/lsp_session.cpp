#include "lsp_session.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
namespace exagent {
LspSession::LspSession(QString r, QString p, QStringList a)
    : root(std::move(r)), program(std::move(p)), arguments(std::move(a)) {
  connect(&process, &QProcess::readyReadStandardOutput, this,
          &LspSession::read);
  connect(&process, &QProcess::readyReadStandardError, this,
          [this] { process.readAllStandardError(); });
  connect(&process, &QProcess::errorOccurred, this, [this] {
    fail(QStringLiteral("语言服务进程不可用：") + process.errorString());
  });
  connect(&process, &QProcess::finished, this, [this] {
    if (status != Failed)
      fail(QStringLiteral("语言服务已退出，请重新连接。"));
  });
  timeout.setInterval(1000);
  connect(&timeout, &QTimer::timeout, this, [this] {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto ids = pending.keys();
    for (int id : ids)
      if (pending.contains(id) && now - pending[id].started > 15000) {
        auto p = pending.take(id);
        notify("$/cancelRequest", {{"id", id}});
        if (p.owner)
          p.reply({}, QStringLiteral("语言服务请求超时。"));
      }
  });
}
LspSession::~LspSession() {
  process.disconnect(this);
  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(1000);
  }
}
void LspSession::start() {
  if (status != Stopped)
    return;
  qInfo().noquote() << QStringLiteral("启动语言服务：工作区=") << root
                    << QStringLiteral("，程序=")
                    << QFileInfo(program).fileName();
  status = Starting;
  emit changed();
  process.setWorkingDirectory(root);
  connect(&process, &QProcess::started, this, [this] {
    timeout.start();
    const auto uri = QUrl::fromLocalFile(root).toString(QUrl::FullyEncoded);
    QJsonObject capabilities{
        {"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}},
        {"textDocument",
         QJsonObject{
             {"synchronization",
              QJsonObject{{"dynamicRegistration", false}, {"didSave", true}}},
             {"completion",
              QJsonObject{{"completionItem",
                           QJsonObject{{"snippetSupport", false},
                                       {"insertReplaceSupport", false}}}}},
             {"publishDiagnostics", QJsonObject{{"versionSupport", true}}}}},
        {"workspace",
         QJsonObject{{"applyEdit", false},
                     {"workspaceFolders", true},
                     {"configuration", true},
                     {"workspaceEdit",
                      QJsonObject{{"documentChanges", true},
                                  {"resourceOperations", QJsonArray{}}}}}}};
    request("initialize",
            {{"processId", int(QCoreApplication::applicationPid())},
             {"rootUri", uri},
             {"capabilities", capabilities},
             {"workspaceFolders",
              QJsonArray{
                  QJsonObject{{"uri", uri}, {"name", root.section('/', -1)}}}}},
            this, [this](QJsonValue result, QString error) {
              if (!error.isEmpty()) {
                fail(error);
                return;
              }
              caps = result.toObject().value("capabilities").toObject();
              const auto encoding =
                  caps.value("positionEncoding").toString("utf-16");
              if (encoding != "utf-16") {
                fail(QStringLiteral("语言服务没有采用UTF-16位置编码。"));
                return;
              }
              notify("initialized", {});
              status = Ready;
              emit changed();
              emit ready();
            });
  });
  process.start(program, arguments);
}
int LspSession::syncKind() const {
  auto v = caps.value("textDocumentSync");
  return v.isDouble() ? v.toInt() : v.toObject().value("change").toInt();
}
void LspSession::send(QJsonObject o) {
  if (status == Failed || process.state() != QProcess::Running)
    return;
  auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
  if (bytes.size() > 8 * 1024 * 1024 ||
      process.bytesToWrite() + bytes.size() > 16 * 1024 * 1024) {
    fail(QStringLiteral("语言服务消息超过缓冲上限。"));
    return;
  }
  process.write("Content-Length: " + QByteArray::number(bytes.size()) +
                "\r\n\r\n" + bytes);
}
void LspSession::notify(const QString &m, const QJsonObject &p) {
  send({{"jsonrpc", "2.0"}, {"method", m}, {"params", p}});
}
void LspSession::request(const QString &m, const QJsonObject &p, QObject *owner,
                         Reply reply) {
  if (status == Failed || pending.size() >= 64) {
    reply({}, message.isEmpty() ? QStringLiteral("语言服务请求队列已满。")
                                : message);
    return;
  }
  const int id = ++nextId;
  pending.insert(
      id, {owner, std::move(reply), QDateTime::currentMSecsSinceEpoch()});
  send({{"jsonrpc", "2.0"}, {"id", id}, {"method", m}, {"params", p}});
}
void LspSession::fail(const QString &why) {
  if (status == Failed)
    return;
  qWarning().noquote() << QStringLiteral("语言服务连接失败：工作区=") << root
                       << QStringLiteral("，原因类别=")
                       << why.section(QChar(0xff1a), 0, 0);
  status = Failed;
  message = why;
  timeout.stop();
  if (process.state() != QProcess::NotRunning)
    process.kill();
  auto old = std::move(pending);
  pending.clear();
  emit changed();
  for (auto &p : old)
    if (p.owner)
      p.reply({}, why);
}
void LspSession::read() {
  input += process.readAllStandardOutput();
  if (input.size() > 8 * 1024 * 1024 + 8192) {
    fail(QStringLiteral("语言服务响应超过8MiB。"));
    return;
  }
  while (status != Failed) {
    if (body < 0) {
      const auto end = input.indexOf("\r\n\r\n");
      if (end < 0) {
        if (input.size() > 8192)
          fail(QStringLiteral("语言服务帧头无效。"));
        return;
      }
      bool found = false;
      for (auto line : input.left(end).split('\n')) {
        line = line.trimmed();
        if (line.toLower().startsWith("content-length:")) {
          bool ok = false;
          body = line.mid(15).trimmed().toInt(&ok);
          if (found || !ok || body <= 0 || body > 8 * 1024 * 1024) {
            fail(QStringLiteral("语言服务帧长度无效。"));
            return;
          }
          found = true;
        }
      }
      if (!found) {
        fail(QStringLiteral("语言服务缺少Content-Length。"));
        return;
      }
      input.remove(0, end + 4);
    }
    if (input.size() < body)
      return;
    QJsonParseError e;
    const auto value = QJsonDocument::fromJson(input.left(body), &e);
    input.remove(0, body);
    body = -1;
    if (e.error != QJsonParseError::NoError || !value.isObject()) {
      fail(QStringLiteral("语言服务JSON响应无效。"));
      return;
    }
    dispatch(value.object());
  }
}
void LspSession::dispatch(QJsonObject o) {
  const auto method = o.value("method").toString();
  if (!method.isEmpty()) {
    if (method == "textDocument/publishDiagnostics") {
      emit diagnostics(o.value("params").toObject());
      return;
    }
    if (o.contains("id")) {
      QJsonValue result;
      if (method == "workspace/configuration") {
        QJsonArray a;
        for (auto unused :
             o.value("params").toObject().value("items").toArray()) {
          Q_UNUSED(unused);
          a.append(QJsonObject{});
        }
        result = a;
      } else if (method == "workspace/workspaceFolders")
        result = QJsonArray{QJsonObject{
            {"uri", QUrl::fromLocalFile(root).toString(QUrl::FullyEncoded)},
            {"name", root.section('/', -1)}}};
      else if (method == "window/workDoneProgress/create")
        result = QJsonValue(QJsonValue::Null);
      else {
        send(
            {{"jsonrpc", "2.0"},
             {"id", o["id"]},
             {"error", QJsonObject{{"code", -32601},
                                   {"message", "Unsupported client method"}}}});
        return;
      }
      send({{"jsonrpc", "2.0"}, {"id", o["id"]}, {"result", result}});
    }
    return;
  }
  const auto id = o.value("id").toInt(-1);
  if (!pending.contains(id))
    return;
  auto p = pending.take(id);
  if (p.owner)
    p.reply(
        o.value("result"),
        o.contains("error")
            ? QStringLiteral("语言服务拒绝请求：") +
                  o.value("error").toObject().value("message").toString().left(
                      2048)
            : QString());
}
} // namespace exagent
