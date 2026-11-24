#pragma once
#include <QObject>
#include <QSet>
#include <QHash>
#include "plugins/workspace_editor/editor_document.h"
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
namespace exagent {
// 工作区组织文件页签，编辑文档独立维护版本与撤销栈。
class WorkspaceController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(WorkspaceController)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY filesChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY filesChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY previewChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY previewChanged)
    Q_PROPERTY(QString activePath READ activePath NOTIFY previewChanged)
    Q_PROPERTY(QString source READ source NOTIFY contentChanged)
    Q_PROPERTY(exagent::EditorDocument* document READ document NOTIFY previewChanged)
    Q_PROPERTY(bool hasUnsaved READ hasUnsaved NOTIFY previewChanged)
    Q_PROPERTY(QString language READ language NOTIFY previewChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit WorkspaceController(QObject *parent = nullptr);
    QString rootPath() const { return m_root; }
    QVariantList entries() const { return m_entries; }
    QVariantList tabs() const;
    int activeTab() const { return m_active; }
    QString activePath() const;
    QString source() const;
    EditorDocument *document() const;
    Q_INVOKABLE EditorDocument *documentAt(int index) const { return index>=0 && index<m_files.size() ? m_files[index].document : nullptr; }
    bool hasUnsaved() const;
    Q_INVOKABLE bool saveAll();
    Q_INVOKABLE bool flushDrafts();
    Q_INVOKABLE void approveExit() { m_exitApproved=true; }
    QString language() const;
    QString error() const { return m_error; }
    Q_INVOKABLE void bindFolder(const QString &path, bool preview);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE void closeTab(int index, bool discard=false);
signals:
    void fileActivated();
    void exitRequested();
    void closeRequested(int index);
    void contentChanged();
    void filesChanged();
    void previewChanged();
    void errorChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool m_exitApproved=false;
    struct File { QString path; QString language; EditorDocument *document; };
    struct ViewState { QList<File> files; int active=-1; };
    QHash<QString,ViewState> m_cache;
    QSet<EditorDocument*> m_documents;
    bool m_preview=false;
    QString resolve(const QString &path) const;
    void scan(const QString &directory, int depth);
    void fail(const QString &message);
    QString m_root;
    QString m_previewRoot;
    QString m_error;
    QVariantList m_entries;
    QList<File> m_files;
    QSet<QString> m_expanded;
    int m_active = -1;
    bool m_restoring = false;
};
}
