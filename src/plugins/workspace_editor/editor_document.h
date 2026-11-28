#pragma once
#include <QFileDevice>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include "plugins/language_services/text_edits.h"
#include "plugins/language_services/language_document.h"
#include "editor_page.h"
class QSyntaxHighlighter;
namespace exagent {
// 文件编辑、磁盘版本和恢复草稿独立于聊天数据及终端进程。
class LanguageDocument;
class EditorPage;
class EditorDocument final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由工作区打开文件")
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(bool largeFile READ largeFile NOTIFY modeChanged)
    Q_PROPERTY(exagent::EditorPage* page READ page CONSTANT)
    Q_PROPERTY(exagent::LanguageDocument* languageService READ languageService CONSTANT)
    Q_PROPERTY(int cursorStart READ cursorStart NOTIFY cursorRequested)
    Q_PROPERTY(int cursorEnd READ cursorEnd NOTIFY cursorRequested)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool editable READ editable CONSTANT)
    Q_PROPERTY(QString reason READ reason CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY historyChanged)
    Q_PROPERTY(bool redoAvailable READ redoAvailable NOTIFY historyChanged)
  public:
    enum State { Clean = 0, Modified = 1, Conflict = 2 };
    Q_ENUM(State)
    EditorDocument(QString path, QByteArray bytes, bool readOnly, QObject *parent);
    ~EditorDocument() override;
    QString path() const {
        return m_path;
    }
    QString text() const {
        return editable() ? m_document.toRawText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
                          : m_readOnlySource;
    }
    int state() const {
        return m_conflict ? Conflict : m_document.isModified() ? Modified : Clean;
    }
    bool editable() const {
        return m_reason.isEmpty();
    }
    QString reason() const {
        return m_reason;
    }
    QString error() const {
        return m_draftError.isEmpty() ? m_error : m_draftError;
    }
    bool largeFile()const{return m_large;}
    EditorPage *page();
    int characterCount()const{return qMax(0,m_document.characterCount()-1);}
    int lineCount()const{return m_document.blockCount();}
    int lineOffset(int line)const;
    int lineAt(int offset)const;
    bool lowSurrogateAt(int offset)const;
    QString rangeText(int start,int end)const;
    bool replaceWindow(int start,const QString &before,const QString &after);
    Q_INVOKABLE void recordCaret(int start,int end){m_cursorStart=start;m_cursorEnd=end;}
    Q_INVOKABLE void undo(){m_document.undo();}
    Q_INVOKABLE void redo(){m_document.redo();}
    LanguageDocument *languageService()const{return m_language;}
    void setLanguageService(LanguageDocument *value){m_language=value;}
    bool applyTextEdits(const QString &expected,const QVector<lsp::Edit> &edits);
    void undoLastEdit(){m_document.undo();}
    void announceContent(){emit contentChanged();emit stateChanged();emit historyChanged();}
    void moveCursor(int start,int end){m_cursorStart=start;m_cursorEnd=end;emit cursorRequested();}
    int cursorStart()const{return m_cursorStart;}
    int cursorEnd()const{return m_cursorEnd;}
    bool undoAvailable() const {
        return m_document.isUndoAvailable();
    }
    bool redoAvailable() const {
        return m_document.isRedoAvailable();
    }
    Q_INVOKABLE void attach(QQuickTextDocument *view, bool dark);
    Q_INVOKABLE bool save(bool overwrite = false);
    Q_INVOKABLE bool reload();
    void checkExternal();
    bool flushDraft();
    static bool hasDraft(const QString &path);
    bool discardDraft();
  signals:
    void historyChanged();
    void cursorRequested();
    void modeChanged();
    void saved();
    void stateChanged();
    void contentChanged();

  private:
    QByteArray readDisk(bool *ok) const;
    void highlight(bool dark);
    void enterLargeMode();
    QString draftPath() const;
    void watch();
    void report(const QString &message);
    bool writeDraft(int state);
    std::unique_ptr<QLockFile> m_lock;
    QString m_readOnlySource;
    QString m_path, m_parent, m_reason, m_error, m_draftError;
    bool m_large=false;
    int m_cursorStart=0,m_cursorEnd=0;
    LanguageDocument *m_language=nullptr;
    EditorPage *m_page=nullptr;
    QByteArray m_expected, m_newline = "\n";
    QFileDevice::Permissions m_permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    bool m_dark = false;
    bool m_bom = false, m_conflict = false, m_loading = false;
    QTextDocument m_document;
    QFileSystemWatcher m_watcher;
    QTimer m_draftTimer, m_watchTimer;
    QPointer<QQuickTextDocument> m_view;
    QSyntaxHighlighter *m_highlighter = nullptr;
};
} // namespace exagent
