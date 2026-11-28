#include "language_document.h"
#include "language_workspace.h"
#include "plugins/workspace_editor/editor_document.h"
#include "text_edits.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
namespace exagent {
namespace {
QString languageFor(QString path) {
  auto ext = QFileInfo(path).suffix().toLower();
  if (ext == "go")
    return "go";
  if (ext == "c" || ext == "h")
    return "c";
  if (QStringList{"cpp", "cc", "cxx", "hpp", "hh"}.contains(ext))
    return "cpp";
  if (ext == "py")
    return "python";
  if (ext == "ts" || ext == "tsx")
    return "typescript";
  if (ext == "js" || ext == "jsx")
    return "javascript";
  if (ext == "rs")
    return "rust";
  return ext;
}
QString configKey(const QString &language) {
  return "language-services/" + language;
}
} // namespace
LanguageDocument::LanguageDocument(EditorDocument *d, LanguageWorkspace *w)
    : QObject(d), doc(d), workspace(w) {
  uri = QUrl::fromLocalFile(doc->path()).toString(QUrl::FullyEncoded);
  languageId = languageFor(doc->path());
  if (!doc->largeFile())
    observedText = doc->text();
  changes.setSingleShot(true);
  changes.setInterval(180);
  connect(&changes, &QTimer::timeout, this, &LanguageDocument::sync);
  connect(doc, &EditorDocument::contentChanged, this, [this] {
    if (doc->largeFile())
      return;
    const auto now = doc->text();
    // 语法着色/Qt视图切换也会发出contentsChanged，但LSP版本只能随实际正文递增。
    if (now == observedText)
      return;
    observedText = now;
    ++revision;
    diagnosticRows.clear();
    completionRows.clear();
    completionItems = {};
    locationRows.clear();
    ++completionSerial;
    ++definitionSerial;
    ++renameSerial;
    discardRename();
    changes.start();
    emit changed();
  });
  connect(doc, &EditorDocument::modeChanged, this, [this] {
    if (doc->largeFile()) {
      observedText.clear();
      disconnectServer();
    }
  });
  connect(doc, &EditorDocument::saved, this, [this] {
    if (connection && isOpen) {
      sync();
      connection->notify("textDocument/didSave",
                         {{"textDocument", QJsonObject{{"uri", uri}}}});
    }
  });
}
LanguageDocument::~LanguageDocument() { disconnectServer(); }
void LanguageDocument::announce(const QString &why) {
  error = why;
  emit changed();
}
QVariantMap LanguageDocument::configuration() const {
  auto map = QSettings().value(configKey(languageId)).toMap();
  if (!map.isEmpty())
    return map;
  QString command, args;
  if (languageId == "go")
    command = "gopls";
  else if (languageId == "c" || languageId == "cpp")
    command = "clangd";
  else if (languageId == "python")
    command = "pylsp";
  else if (languageId == "rust")
    command = "rust-analyzer";
  else if (languageId == "typescript" || languageId == "javascript") {
    command = "typescript-language-server";
    args = "--stdio";
  }
  auto program = QStandardPaths::findExecutable(command);
  if (program.isEmpty())
    program = QStandardPaths::findExecutable(
        command,
        {"/opt/homebrew/bin", "/usr/local/bin", QDir::homePath() + "/go/bin",
         QDir::homePath() + "/.cargo/bin"});
  return {{"program", program}, {"arguments", args}};
}
void LanguageDocument::connectServer(const QString &program,
                                     const QString &arguments) {
  if (!workspace || !doc->editable() || doc->largeFile()) {
    announce(QStringLiteral("当前文件处于只读或大文件模式，不启动语言服务。"));
    return;
  }
  const QFileInfo file(program);
  if (!file.isAbsolute() || !file.isFile() || !file.isExecutable() ||
      arguments.size() > 8192) {
    announce(QStringLiteral("请选择有效的语言服务器绝对路径。"));
    return;
  }
  disconnectServer();
  QSettings().setValue(
      configKey(languageId),
      QVariantMap{{"program", program}, {"arguments", arguments}});
  connection =
      workspace->session(program, arguments.split('\n', Qt::SkipEmptyParts));
  error.clear();
  connect(connection.get(), &LspSession::changed, this,
          [this] { emit changed(); });
  connect(connection.get(), &LspSession::ready, this,
          &LanguageDocument::opened);
  connect(connection.get(), &LspSession::diagnostics, this,
          &LanguageDocument::publish);
  if (connection->state() == LspSession::Ready)
    opened();
  else
    connection->start();
  emit changed();
}
void LanguageDocument::disconnectServer() {
  changes.stop();
  if (connection) {
    if (isOpen)
      connection->notify("textDocument/didClose",
                         {{"textDocument", QJsonObject{{"uri", uri}}}});
    connection->disconnect(this);
  }
  isOpen = false;
  connection.reset();
  completionRows.clear();
  completionItems = {};
  diagnosticRows.clear();
  locationRows.clear();
  ++completionSerial;
  ++definitionSerial;
  ++renameSerial;
  discardRename();
  emit changed();
}
void LanguageDocument::opened() {
  if (isOpen || !connection || connection->state() != LspSession::Ready)
    return;
  lastText = doc->text();
  isOpen = true;
  connection->notify("textDocument/didOpen",
                     {{"textDocument", QJsonObject{{"uri", uri},
                                                   {"languageId", languageId},
                                                   {"version", revision},
                                                   {"text", lastText}}}});
  emit changed();
}
void LanguageDocument::sync() {
  changes.stop();
  if (!connection || !isOpen || connection->state() != LspSession::Ready)
    return;
  const auto now = doc->text();
  if (now == lastText)
    return;
  const int kind = connection->syncKind();
  if (kind != 1 && kind != 2) {
    announce(QStringLiteral("语言服务未声明文档同步能力。"));
    return;
  }
  QJsonObject change{{"text", now}};
  if (kind == 2)
    change.insert(
        "range",
        QJsonObject{{"start", QJsonObject{{"line", 0}, {"character", 0}}},
                    {"end", lsp::position(lastText, lastText.size())}});
  connection->notify(
      "textDocument/didChange",
      {{"textDocument", QJsonObject{{"uri", uri}, {"version", revision}}},
       {"contentChanges", QJsonArray{change}}});
  lastText = now;
}
bool LanguageDocument::synchronized() const {
  return connection && connection->state() == LspSession::Ready && isOpen &&
         lastText == doc->text();
}
bool LanguageDocument::usable(const QString &capability) {
  if (!workspace || !connection || connection->state() != LspSession::Ready ||
      !isOpen) {
    announce(QStringLiteral("请先连接语言服务。"));
    return false;
  }
  const auto value = connection->capabilities().value(capability);
  if (value.isUndefined() || value == false) {
    announce(QStringLiteral("语言服务不支持此能力。"));
    return false;
  }
  sync();
  if (!synchronized()) {
    announce(QStringLiteral("正文尚未成功同步，未发送语言功能请求。"));
    return false;
  }
  error.clear();
  return true;
}
QJsonObject LanguageDocument::at(int cursor) const {
  return {{"textDocument", QJsonObject{{"uri", uri}}},
          {"position", lsp::position(doc->text(), cursor)}};
}
void LanguageDocument::complete(int cursor) {
  if (!usable("completionProvider"))
    return;
  const auto text = doc->text();
  if (cursor < 0 || cursor > text.size())
    return;
  const auto serial = ++completionSerial;
  const int v = revision;
  completionCursor = cursor;
  completionStart = cursor;
  while (completionStart > 0 && (text[completionStart - 1].isLetterOrNumber() ||
                                 text[completionStart - 1] == '_'))
    --completionStart;
  connection->request(
      "textDocument/completion", at(cursor), this,
      [this, serial, v](QJsonValue result, QString why) {
        if (serial != completionSerial || v != revision)
          return;
        if (!why.isEmpty()) {
          announce(why);
          return;
        }
        completionItems = {};
        completionRows.clear();
        completionVersion = v;
        const auto items = result.isArray()
                               ? result.toArray()
                               : result.toObject().value("items").toArray();
        for (const auto &value : items) {
          const auto item = value.toObject();
          if (item.value("insertTextFormat").toInt(1) == 2 ||
              item.value("label").toString().isEmpty())
            continue;
          completionItems.append(item);
          completionRows.append(QVariantMap{
              {"label", item.value("label").toString().left(300)},
              {"detail", item.value("detail").toString().left(400)}});
          if (completionRows.size() >= 100)
            break;
        }
        emit changed();
        emit completionsReady();
      });
}
bool LanguageDocument::acceptCompletion(int index) {
  if (index < 0 || index >= completionItems.size() ||
      completionVersion != revision)
    return false;
  auto item = completionItems[index].toObject();
  auto edit = item.value("textEdit").toObject();
  const auto text = doc->text();
  if (edit.contains("insert") || edit.contains("replace")) {
    announce(QStringLiteral("暂不接受InsertReplace补全。"));
    return false;
  }
  if (edit.isEmpty())
    edit = {
        {"range", QJsonObject{{"start", lsp::position(text, completionStart)},
                              {"end", lsp::position(text, completionCursor)}}},
        {"newText",
         item.value("insertText").toString(item.value("label").toString())}};
  auto edits = item.value("additionalTextEdits").toArray();
  edits.append(edit);
  QVector<lsp::Edit> parsed;
  QString why;
  if (!lsp::edits(text, edits, &parsed, &why)) {
    announce(why);
    return false;
  }
  const auto primary = lsp::offset(
      text, edit.value("range").toObject().value("start").toObject());
  int next = primary + edit.value("newText").toString().size();
  for (const auto &e : parsed)
    if (e.start < primary)
      next += e.text.size() - (e.end - e.start);
  if (!doc->applyTextEdits(text, parsed)) {
    announce(doc->error());
    return false;
  }
  doc->moveCursor(next, next);
  completionRows.clear();
  completionItems = {};
  emit changed();
  return true;
}
void LanguageDocument::publish(QJsonObject p) {
  if (p.value("uri").toString() != uri || !isOpen)
    return;
  if (p.value("version").isDouble() && p.value("version").toInt() != revision)
    return;
  diagnosticRows.clear();
  const auto text = doc->text();
  for (const auto &v : p.value("diagnostics").toArray()) {
    const auto item = v.toObject(), range = item.value("range").toObject();
    const auto start = lsp::offset(text, range.value("start").toObject()),
               end = lsp::offset(text, range.value("end").toObject());
    if (start < 0 || end < start)
      continue;
    diagnosticRows.append(QVariantMap{
        {"start", start},
        {"end", end},
        {"line", range.value("start").toObject().value("line").toInt() + 1},
        {"severity", item.value("severity").toInt(1)},
        {"message", item.value("message").toString().left(2048)}});
    if (diagnosticRows.size() >= 500)
      break;
  }
  emit changed();
}
void LanguageDocument::selectDiagnostic(int i) {
  if (i >= 0 && i < diagnosticRows.size()) {
    const auto r = diagnosticRows[i].toMap();
    doc->moveCursor(r["start"].toInt(), r["end"].toInt());
  }
}
void LanguageDocument::definition(int cursor) {
  if (!usable("definitionProvider"))
    return;
  const auto serial = ++definitionSerial;
  const auto v = revision;
  connection->request(
      "textDocument/definition", at(cursor), this,
      [this, serial, v](QJsonValue result, QString why) {
        if (!workspace || serial != definitionSerial || v != revision)
          return;
        if (!why.isEmpty()) {
          announce(why);
          return;
        }
        locationRows.clear();
        auto values = result.isArray() ? result.toArray() : QJsonArray{result};
        for (auto value : values) {
          auto row = value.toObject();
          const auto path = workspace->safePath(
              row.value("uri").toString(row.value("targetUri").toString()));
          auto range = row.value("range").toObject();
          if (range.isEmpty())
            range = row.value("targetSelectionRange").toObject();
          const auto pos = range.value("start").toObject();
          if (path.isEmpty() || pos.isEmpty())
            continue;
          locationRows.append(QVariantMap{
              {"path", path},
              {"label", QDir(workspace->root()).relativeFilePath(path) + ":" +
                            QString::number(pos.value("line").toInt() + 1)},
              {"line", pos.value("line").toInt()},
              {"character", pos.value("character").toInt()}});
          if (locationRows.size() >= 100)
            break;
        }
        if (locationRows.isEmpty())
          error = QStringLiteral("没有找到工作区内的定义。");
        emit changed();
        if (locationRows.size() == 1)
          openDefinition(0);
        else
          emit definitionsReady();
      });
}
void LanguageDocument::openDefinition(int index) {
  if (!workspace || index < 0 || index >= locationRows.size())
    return;
  auto row = locationRows[index].toMap();
  emit workspace->openLocation(row["path"].toString(), row["line"].toInt(),
                               row["character"].toInt());
}
void LanguageDocument::rename(int cursor, const QString &name) {
  if (name.trimmed().isEmpty() || name.size() > 256 ||
      !usable("renameProvider"))
    return;
  const auto serial = ++renameSerial;
  const auto v = revision;
  auto params = at(cursor);
  params.insert("newName", name);
  const auto snapshot = workspace->snapshot(this);
  connection->request(
      "textDocument/rename", params, this,
      [this, serial, v, snapshot](QJsonValue result, QString why) {
        if (!workspace || serial != renameSerial || v != revision)
          return;
        if (!why.isEmpty()) {
          announce(why);
          return;
        }
        if (!result.isObject()) {
          announce(QStringLiteral("语言服务未返回重命名修改。"));
          return;
        }
        if (!workspace->prepareRename(this, result.toObject(), snapshot,
                                      &why)) {
          announce(why);
          return;
        }
        emit changed();
        emit renameReady();
      });
}
bool LanguageDocument::applyRename() {
  if (!workspace) {
    announce(QStringLiteral("工作区已关闭。"));
    return false;
  }
  QString why;
  if (!workspace->applyRename(this, &why)) {
    discardRename();
    announce(why);
    return false;
  }
  discardRename();
  return true;
}
void LanguageDocument::discardRename() {
  if (workspace)
    workspace->discardRename(this);
  if (!renamePreview.isEmpty()) {
    renamePreview.clear();
    emit changed();
  }
}
} // namespace exagent
