#include "viewer_data.h"
#include "viewer_registry.h"
#include <QtConcurrent>
#include <QClipboard>
#include <QGuiApplication>
namespace exagent {
QVariant ViewerTableModel::data(const QModelIndex &index,int role) const {
    if (!index.isValid() || index.row()>=m_rows.size() || role!=Qt::DisplayRole) return {};
    return m_rows[index.row()].toStringList().value(index.column());
}
void ViewerTableModel::replace(const QVariantList &rows) {
    beginResetModel(); m_rows=rows; m_columns=0;
    for (const auto &row:m_rows) m_columns=qMax(m_columns,int(row.toStringList().size()));
    endResetModel();
}
WorkspaceViewerData::WorkspaceViewerData(QObject *parent):QObject(parent) {
    m_delay.setSingleShot(true); m_delay.setInterval(100);
    connect(this,&WorkspaceViewerData::inputChanged,this,[this]{ ++m_generation; ++m_textGeneration; ++m_findGeneration; release(); m_content={}; m_table.replace({}); m_pageText.clear(); m_state=Loading; emit changed(); emit pageTextChanged(); m_delay.start(); });
    connect(&m_delay,&QTimer::timeout,this,&WorkspaceViewerData::reload);
    connect(&m_job,&QFutureWatcher<QVariantMap>::finished,this,[this]{
        auto value=m_job.result();
        if (m_running!=m_generation) { WorkspacePdfProvider::release(value.value("token").toString()); m_published=m_running; m_delay.start(); return; }
        // 大表仅通过虚拟表格模型暴露单元格，不能把整张表同步转换成 QML 对象树。
        const auto rows=value.take("rows").toList();
        release(); m_published=m_running; m_content=value; m_table.replace(rows);
        m_state=value.contains("error") ? Failed : Ready; emit changed();
    });
    connect(&m_text,&QFutureWatcher<QString>::finished,this,[this]{ if (m_textRunning==m_textGeneration) { m_pageText=m_text.result(); emit pageTextChanged(); } else if (m_state==Ready) readPdfText(m_requestedPage); });
    connect(&m_find,&QFutureWatcher<QPoint>::finished,this,[this]{ if (m_findRunning==m_findGeneration) { auto point=m_find.result(); emit found(point.y(),point.x()); } else if (m_state==Ready) find(m_query); });
}
WorkspaceViewerData::~WorkspaceViewerData() {
    // 取消发布而不终止正在解码的线程；每项任务只持有独立快照。
    if (m_job.future().isValid() && m_running!=0 && m_running!=m_published) {
        auto *cleanup=new QFutureWatcher<QVariantMap>;
        connect(cleanup,&QFutureWatcher<QVariantMap>::finished,cleanup,[cleanup]{ WorkspacePdfProvider::release(cleanup->result().value("token").toString()); cleanup->deleteLater(); });
        cleanup->setFuture(m_job.future());
    }
    release();
}
void WorkspaceViewerData::release() { WorkspacePdfProvider::release(m_content.value("token").toString()); }
void WorkspaceViewerData::reload() {
    // 后台完成但完成信号尚未投递时，也必须先处理旧结果，不能覆盖 watcher。
    if (m_running!=m_published) { ++m_generation; return; }
    ++m_generation; ++m_textGeneration; ++m_findGeneration; m_running=m_generation;
    release(); m_content={}; m_table.replace({}); m_pageText.clear(); emit pageTextChanged();
    m_state=Loading; emit changed();
    const auto path=m_path,root=m_root;
    m_job.setFuture(QtConcurrent::run([path,root]{ return WorkspaceViewerRegistry::instance().load(path,root); }));
}
void WorkspaceViewerData::copy(const QString &text) { QGuiApplication::clipboard()->setText(text); }
void WorkspaceViewerData::readPdfText(int page) {
    m_requestedPage=page; ++m_textGeneration; m_pageText.clear(); emit pageTextChanged();
    if (m_text.isRunning()) return;
    m_textRunning=m_textGeneration; const auto token=m_content.value("token").toString();
    m_text.setFuture(QtConcurrent::run([token,page]{return WorkspacePdfProvider::text(token,page);}));
}
void WorkspaceViewerData::find(const QString &query) {
    m_query=query; ++m_findGeneration;
    if (m_find.isRunning()) return;
    m_findRunning=m_findGeneration; const auto rows=m_table.rows();
    m_find.setFuture(QtConcurrent::run([rows,query]{
        if (query.isEmpty()) return QPoint(-1,-1);
        for (int r=0;r<rows.size();++r) { const auto columns=rows[r].toStringList(); for (int c=0;c<columns.size();++c) if (columns[c].contains(query,Qt::CaseInsensitive)) return QPoint(c,r); }
        return QPoint(-1,-1);
    }));
}
}
