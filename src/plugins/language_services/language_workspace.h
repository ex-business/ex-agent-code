#pragma once
#include "text_edits.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSharedPointer>
#include <QVariantList>
namespace exagent {
class EditorDocument;
class LanguageDocument;
class LspSession;
class LanguageWorkspace final : public QObject {
  Q_OBJECT
public:
  explicit LanguageWorkspace(QString root, QObject *parent);
  QString root() const { return folder; }
  void setActive(bool value);
  LanguageDocument *add(EditorDocument *document);
  QSharedPointer<LspSession> session(QString program, QStringList arguments);
  QString safePath(QString uri) const;
  struct RenameSnapshot {
    QHash<QString, QString> text;
    QHash<QString, QPointer<EditorDocument>> documents;
    QSet<QString> unsynchronized;
    qint64 started = 0;
  };
  RenameSnapshot snapshot(LanguageDocument *owner);
  bool prepareRename(LanguageDocument *owner, QJsonObject edit,
                     const RenameSnapshot &snapshot, QString *error);
  bool applyRename(LanguageDocument *owner, QString *error);
  void discardRename(LanguageDocument *owner);
signals:
  void documentPrepared(exagent::EditorDocument *document);
  void openLocation(QString path, int line, int character);

private:
  struct Planned {
    QPointer<EditorDocument> document;
    QString before;
    QVector<lsp::Edit> edits;
  };
  struct Plan {
    QVector<Planned> files;
  };
  QString folder;
  bool active = true;
  QHash<QString, QPointer<EditorDocument>> documents;
  QHash<QString, QWeakPointer<LspSession>> sessions;
  QHash<LanguageDocument *, Plan> plans;
};
} // namespace exagent
