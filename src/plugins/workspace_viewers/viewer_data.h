#pragma once
#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QQuickImageProvider>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
namespace exagent {
class ViewerTableModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent={}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex &parent={}) const override { return parent.isValid() ? 0 : m_columns; }
    QVariant data(const QModelIndex &index,int role) const override;
    void replace(const QVariantList &rows);
    QVariantList rows() const { return m_rows; }
private: QVariantList m_rows; int m_columns=0;
};
class WorkspaceViewerData : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString filePath MEMBER m_path NOTIFY inputChanged)
    Q_PROPERTY(QString rootPath MEMBER m_root NOTIFY inputChanged)
    Q_PROPERTY(QVariantMap content READ content NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* table READ table CONSTANT)
    Q_PROPERTY(QString pageText READ pageText NOTIFY pageTextChanged)
public:
    enum State { Idle=0,Loading=1,Ready=2,Failed=3 }; Q_ENUM(State)
    explicit WorkspaceViewerData(QObject *parent=nullptr);
    ~WorkspaceViewerData() override;
    QVariantMap content() const { return m_content; }
    int state() const { return m_state; }
    QAbstractItemModel *table() { return &m_table; }
    QString pageText() const { return m_pageText; }
    Q_INVOKABLE void reload();
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE QString cellValue(int row,int column) const { return m_table.data(m_table.index(row,column),Qt::DisplayRole).toString(); }
    Q_INVOKABLE void readPdfText(int page);
    Q_INVOKABLE void find(const QString &query);
signals:
    void inputChanged(); void changed(); void pageTextChanged();
    void found(int row,int column);
private:
    void release();
    QString m_path,m_root,m_pageText,m_query;
    int m_requestedPage=0;
    int m_state=Idle;
    quint64 m_published=0,m_generation=0,m_running=0,m_textGeneration=0,m_textRunning=0,m_findGeneration=0,m_findRunning=0;
    QVariantMap m_content;
    ViewerTableModel m_table;
    QTimer m_delay;
    QFutureWatcher<QVariantMap> m_job;
    QFutureWatcher<QString> m_text;
    QFutureWatcher<QPoint> m_find;
};
class WorkspacePdfProvider final : public QQuickImageProvider {
public:
    WorkspacePdfProvider();
    QImage requestImage(const QString &id,QSize *size,const QSize &requested) override;
    static QVariantMap registerPdf(const QByteArray &bytes);
    static void release(const QString &token);
    static QString text(const QString &token,int page);
};
}
