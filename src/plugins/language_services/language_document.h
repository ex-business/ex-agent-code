#pragma once
#include "lsp_session.h"
#include <QJsonArray>
#include <QSharedPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
namespace exagent {
class EditorDocument;
class LanguageWorkspace;
class LanguageDocument final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("由编辑文档创建")
  Q_PROPERTY(int state READ state NOTIFY changed)
  Q_PROPERTY(int completionPosition READ completionPosition NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QString language READ language CONSTANT)
  Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY changed)
  Q_PROPERTY(QVariantList completions READ completions NOTIFY changed)
  Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY changed)
  Q_PROPERTY(QVariantList locations READ locations NOTIFY changed)
  Q_PROPERTY(QVariantList renamePreview MEMBER renamePreview NOTIFY changed)
public:
  LanguageDocument(EditorDocument *document, LanguageWorkspace *workspace);
  ~LanguageDocument() override;
  int state() const { return connection ? connection->state() : 0; }
  int completionPosition() const { return completionCursor; }
  QString message() const {
    return error.isEmpty() ? (connection ? connection->error() : QString())
                           : error;
  }
  QString language() const { return languageId; }
  QVariantMap configuration() const;
  QVariantList completions() const { return completionRows; }
  QVariantList diagnostics() const { return diagnosticRows; }
  QVariantList locations() const { return locationRows; }
  QVariantList renamePreview;
  EditorDocument *document() const { return doc; }
  int version() const { return revision; }
  bool isConnected() const { return isOpen; }
  LspSession *sessionIdentity() const { return connection.get(); }
  void synchronize() { sync(); }
  bool synchronized() const;
  Q_INVOKABLE void connectServer(const QString &program,
                                 const QString &arguments);
  Q_INVOKABLE void disconnectServer();
  Q_INVOKABLE void complete(int cursor);
  Q_INVOKABLE bool acceptCompletion(int index);
  Q_INVOKABLE void definition(int cursor);
  Q_INVOKABLE void openDefinition(int index);
  Q_INVOKABLE void rename(int cursor, const QString &name);
  Q_INVOKABLE bool applyRename();
  Q_INVOKABLE void discardRename();
  Q_INVOKABLE void selectDiagnostic(int index);
  void announce(const QString &reason);
signals:
  void changed();
  void completionsReady();
  void definitionsReady();
  void renameReady();

private:
  void opened();
  void sync();
  bool usable(const QString &capability);
  void publish(QJsonObject params);
  QJsonObject at(int cursor) const;
  EditorDocument *doc;
  QPointer<LanguageWorkspace> workspace;
  QSharedPointer<LspSession> connection;
  QString uri, languageId, error, lastText, observedText;
  int revision = 1, completionVersion = 0, completionCursor = 0,
      completionStart = 0;
  quint64 completionSerial = 0, definitionSerial = 0, renameSerial = 0;
  bool isOpen = false;
  QTimer changes;
  QJsonArray completionItems;
  QVariantList completionRows, diagnosticRows, locationRows;
};
} // namespace exagent
