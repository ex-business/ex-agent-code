#include "language_workspace.h"
#include "language_document.h"
#include "plugins/workspace_editor/editor_document.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QUrl>
#include <memory>
#include <vector>
namespace exagent {
LanguageWorkspace::LanguageWorkspace(QString r, QObject *p)
    : QObject(p), folder(std::move(r)) {}
void LanguageWorkspace::setActive(bool value) {
  if (active == value)
    return;
  active = value;
  if (!active) {
    plans.clear();
    for (auto doc : documents)
      if (doc && doc->languageService())
        doc->languageService()->disconnectServer();
  }
}
LanguageDocument *LanguageWorkspace::add(EditorDocument *doc) {
  documents.insert(doc->path(), doc);
  connect(doc, &QObject::destroyed, this, [this, path = doc->path(), doc] {
    if (!documents.value(path) || documents.value(path).data() == doc)
      documents.remove(path);
  });
  auto *language = new LanguageDocument(doc, this);
  doc->setLanguageService(language);
  return language;
}
QSharedPointer<LspSession> LanguageWorkspace::session(QString program,
                                                      QStringList args) {
  const auto key = QString::fromUtf8(
      QJsonDocument(QJsonArray{program, QJsonArray::fromStringList(args)})
          .toJson(QJsonDocument::Compact));
  auto shared = sessions.value(key).toStrongRef();
  if (!shared || shared->state() == LspSession::Failed) {
    shared = QSharedPointer<LspSession>::create(folder, program, args);
    sessions.insert(key, shared);
  }
  return shared;
}
QString LanguageWorkspace::safePath(QString uri) const {
  const QUrl url(uri, QUrl::StrictMode);
  if (!url.isLocalFile() || !url.host().isEmpty())
    return {};
  const auto path = url.toLocalFile();
  const QFileInfo f(path);
  const auto canonical = f.canonicalFilePath();
  if (!f.isFile() || f.isSymLink() || canonical != QDir::cleanPath(path) ||
      !canonical.startsWith(folder.endsWith('/') ? folder : folder + '/'))
    return {};
  return canonical;
}
void LanguageWorkspace::discardRename(LanguageDocument *owner) {
  plans.remove(owner);
}
LanguageWorkspace::RenameSnapshot
LanguageWorkspace::snapshot(LanguageDocument *owner) {
  // 重命名前先把共享服务的所有待发正文排进同一stdio队列，不能只同步发起页签。
  for (auto doc : documents)
    if (doc && doc->languageService() &&
        doc->languageService()->sessionIdentity() == owner->sessionIdentity())
      doc->languageService()->synchronize();
  RenameSnapshot result;
  result.started = QDateTime::currentMSecsSinceEpoch();
  for (auto it = documents.begin(); it != documents.end(); ++it)
    if (it.value() && !it.value()->largeFile()) {
      auto *doc = it.value().data();
      result.text.insert(it.key(), doc->text());
      result.documents.insert(it.key(), doc);
      const auto *language = doc->languageService();
      if ((language &&
           language->sessionIdentity() == owner->sessionIdentity() &&
           !language->synchronized()) ||
          (doc->state() != EditorDocument::Clean &&
           (!language || !language->isConnected() ||
            language->sessionIdentity() != owner->sessionIdentity())))
        result.unsynchronized.insert(it.key());
    }
  return result;
}
bool LanguageWorkspace::prepareRename(LanguageDocument *owner, QJsonObject edit,
                                      const RenameSnapshot &snapshot,
                                      QString *error) {
  if (!active) {
    *error = QStringLiteral("工作区已切换，请重新发起重命名。");
    return false;
  }
  plans.remove(owner);
  owner->renamePreview.clear();
  QHash<QString, QJsonArray> files;
  QHash<QString, int> versions;
  if (edit.contains("documentChanges") && edit.contains("changes")) {
    *error = QStringLiteral("重命名返回了重复的修改表示。");
    return false;
  }
  if (edit.contains("changes")) {
    const auto changes = edit.value("changes").toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it) {
      const auto path = safePath(it.key());
      if (path.isEmpty() || !it.value().isArray()) {
        *error = QStringLiteral("重命名包含工作区外或无效文件。");
        return false;
      }
      files.insert(path, it.value().toArray());
    }
  } else
    for (auto value : edit.value("documentChanges").toArray()) {
      auto change = value.toObject();
      if (change.contains("kind")) {
        *error = QStringLiteral("重命名不允许创建、删除或移动文件。");
        return false;
      }
      const auto identifier = change.value("textDocument").toObject();
      const auto path = safePath(identifier.value("uri").toString());
      if (path.isEmpty() || files.contains(path) ||
          !change.value("edits").isArray()) {
        *error = QStringLiteral("重命名文件无效或重复。");
        return false;
      }
      files.insert(path, change.value("edits").toArray());
      if (identifier.value("version").isDouble())
        versions.insert(path, identifier.value("version").toInt());
    }
  if (files.isEmpty() || files.size() > 16) {
    *error = QStringLiteral("重命名没有修改，或超过16个文件上限。");
    return false;
  }
  int newCount = 0;
  for (auto it = files.begin(); it != files.end(); ++it)
    if (!documents.value(it.key()))
      ++newCount;
  if (documents.size() + newCount > 32) {
    *error = QStringLiteral("重命名需要打开过多编辑缓冲，请先关闭部分文件。");
    return false;
  }
  // 先核对所有文件与范围，再发布预览；任何失败都不能修改已有正文。
  Plan plan;
  int totalEdits = 0;
  for (auto it = files.begin(); it != files.end(); ++it) {
    auto *doc = documents.value(it.key()).data();
    if (snapshot.text.contains(it.key())) {
      if (!snapshot.documents.value(it.key()) ||
          doc != snapshot.documents.value(it.key()).data() || !doc ||
          doc->text() != snapshot.text.value(it.key()) ||
          snapshot.unsynchronized.contains(it.key())) {
        *error = QStringLiteral(
            "重命名期间目标缓冲发生变化或未同步，已取消全部修改。");
        return false;
      }
    } else if ((doc && doc->state() != EditorDocument::Clean) ||
               QFileInfo(it.key()).lastModified().toMSecsSinceEpoch() >
                   snapshot.started) {
      *error = QStringLiteral("重命名期间目标文件发生变化，请重试。");
      return false;
    }
    if (!doc) {
      QFile file(it.key());
      if (file.size() > 512 * 1024 || !file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("重命名文件不可读取或处于大文件模式。");
        return false;
      }
      const auto bytes = file.read(512 * 1024 + 1);
      if (bytes.contains('\0') || bytes.size() > 512 * 1024) {
        *error = QStringLiteral("重命名目标不是受支持的文本文件。");
        return false;
      }
      // 缓冲交给工作区后一直保留，不在失败路径删除验收或用户内容。
      doc = new EditorDocument(it.key(), bytes, false, this);
      add(doc);
      emit documentPrepared(doc);
    }
    if (!doc->editable() || doc->largeFile() ||
        doc->state() == EditorDocument::Conflict) {
      *error = QStringLiteral("重命名目标只读、过大或存在磁盘冲突。");
      return false;
    }
    if (versions.contains(it.key())) {
      const auto *language = doc->languageService();
      if (!language || !language->isConnected() ||
          language->version() != versions.value(it.key())) {
        *error = QStringLiteral("重命名的文件版本已过期。");
        return false;
      }
    }
    const auto text = doc->text();
    QVector<lsp::Edit> parsed;
    if (!lsp::edits(text, it.value(), &parsed, error))
      return false;
    totalEdits += parsed.size();
    if (totalEdits > 2000) {
      *error = QStringLiteral("重命名超过2000处修改。");
      return false;
    }
    plan.files.append({doc, text, parsed});
  }
  for (const auto &file : plan.files) {
    QVariantList examples;
    for (const auto &e : file.edits) {
      examples.append(QVariantMap{
          {"line",
           lsp::position(file.before, e.start).value("line").toInt() + 1},
          {"before", file.before.mid(e.start, e.end - e.start).left(100)},
          {"after", e.text.left(100)}});
      if (examples.size() >= 3)
        break;
    }
    owner->renamePreview.append(QVariantMap{
        {"path", QDir(folder).relativeFilePath(file.document->path())},
        {"count", file.edits.size()},
        {"examples", examples}});
  }
  plans.insert(owner, std::move(plan));
  return true;
}
bool LanguageWorkspace::applyRename(LanguageDocument *owner, QString *error) {
  if (!active || !plans.contains(owner)) {
    *error = QStringLiteral("重命名预览已过期。");
    return false;
  }
  auto plan = plans.take(owner);
  for (auto &file : plan.files) {
    if (!file.document || !file.document->editable() ||
        file.document->text() != file.before) {
      *error = QStringLiteral("预览后正文发生变化，已取消全部修改。");
      return false;
    }
    file.document->checkExternal();
    if (file.document->state() == EditorDocument::Conflict) {
      *error = QStringLiteral("预览后磁盘文件发生变化，已取消全部修改。");
      return false;
    }
  }
  // 修改仅进入各文件的可撤销缓冲；先屏蔽跨文件的中间通知，全部完成后再同步LSP和界面。
  std::vector<std::unique_ptr<QSignalBlocker>> blocks;
  for (auto &f : plan.files)
    blocks.push_back(std::make_unique<QSignalBlocker>(f.document));
  int applied = 0;
  for (auto &f : plan.files) {
    if (!f.document->applyTextEdits(f.before, f.edits)) {
      for (int i = applied - 1; i >= 0; --i)
        plan.files[i].document->undoLastEdit();
      *error = QStringLiteral("重命名缓冲更新失败，已撤销本次修改。");
      blocks.clear();
      for (auto &d : plan.files)
        if (d.document)
          d.document->announceContent();
      return false;
    }
    ++applied;
  }
  blocks.clear();
  for (auto &f : plan.files)
    f.document->announceContent();
  return true;
}
} // namespace exagent
