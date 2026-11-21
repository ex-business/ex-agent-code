#pragma once
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
namespace exagent {
// 工作区只读浏览与预览独立于聊天、Agent 和 Git，不执行文件内的命令。
class WorkspaceController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(WorkspaceController)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY filesChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY filesChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY previewChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY previewChanged)
    Q_PROPERTY(QString activePath READ activePath NOTIFY previewChanged)
    Q_PROPERTY(QString source READ source NOTIFY previewChanged)
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
    QString language() const;
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE void closeTab(int index);
signals:
    void filesChanged();
    void previewChanged();
    void errorChanged();
private:
    struct File { QString path; QString source; QString language; };
    QString resolve(const QString &path) const;
    void scan(const QString &directory, int depth);
    void fail(const QString &message);
    QString m_root;
    QString m_error;
    QVariantList m_entries;
    QList<File> m_files;
    QSet<QString> m_expanded;
    int m_active = -1;
};
}
