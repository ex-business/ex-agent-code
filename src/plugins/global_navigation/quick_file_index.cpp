#include "quick_file_index.h"
#include <QDir>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include "plugins/git_changes/git_read.h"
#include <QPromise>

namespace exagent {
namespace {
struct Index { QStringList files; qsizetype tracked = 0; bool limited = false; };
constexpr qsizetype entryLimit = 100000;
void collect(QPromise<Index> &promise, const QString &root, const std::shared_ptr<std::atomic_bool> &cancel)
{
    Index index;
    QSet<QString> known;
    const QDir folder(root);
    // Git 文件优先发布；即使忽略目录很大，已跟踪源码也能提前进入搜索结果。
    if (QFileInfo::exists(folder.filePath(".git"))) {
        const auto output = gitread::runGit(root, {"ls-files","-z","--cached","--","."}, cancel, 8*1024*1024);
        qsizetype start=0, end;
        while (!cancel->load() && (end=output.data.indexOf('\0',start))>=0) {
            const auto path=QFile::decodeName(output.data.mid(start,end-start));
            start=end+1;
            const QFileInfo file(folder.filePath(path));
            if (known.contains(path) || !file.isFile() || file.canonicalFilePath()!=file.absoluteFilePath()) continue;
            known.insert(path); index.files.append(path);
            if (index.files.size()>=entryLimit) { index.limited=true; break; }
        }
        if (cancel->load()) return;
        index.tracked=index.files.size();
        if (index.tracked) promise.addResult(index);
        if (index.limited) return;
    }
    QStringList directories{QString{}};
    // 只索引文件名；不跟随符号链接，不进入 Git 内部目录，切换或关闭时取消旧扫描。
    for (qsizetype i = 0; i < directories.size() && !cancel->load(); ++i) {
        const QString prefix = directories.at(i);
        QDirIterator entries(QDir(root).filePath(prefix),
            QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot);
        while (entries.hasNext()) {
            if (cancel->load()) return;
            const auto entry = entries.nextFileInfo();
            if (entry.fileName().compare(".git",Qt::CaseInsensitive) == 0) continue;
            const auto relative = prefix + entry.fileName();
            if (entry.isDir()) directories.append(relative + '/');
            else if (entry.isFile() && !known.contains(relative)) index.files.append(relative);
            if (index.files.size() + directories.size() >= entryLimit) { index.limited = true; promise.addResult(index); return; }
        }
    }
    if (!cancel->load()) promise.addResult(index);
}
}
QuickFileIndex::~QuickFileIndex() { if (m_cancel) m_cancel->store(true); }

void QuickFileIndex::scan(const QString &root)
{
    if (m_cancel) m_cancel->store(true);
    m_files.clear(); m_limited = false;
    m_root = root.isEmpty() ? QString{} : QFileInfo(root).canonicalFilePath();
    m_loading = !m_root.isEmpty();
    emit changed();
    if (!m_loading) return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    auto *watch = new QFutureWatcher<Index>(this);
    connect(watch, &QFutureWatcher<Index>::resultReadyAt, this, [this, watch, cancel](int i) {
        if (cancel->load()) return;
        const auto index = watch->resultAt(i);
        m_files = index.files; m_tracked = index.tracked; m_limited = index.limited;
        emit changed();
    });
    connect(watch, &QFutureWatcher<Index>::finished,this,[this,watch,cancel] {
        watch->deleteLater();
        if (!cancel->load()) { m_loading=false; emit changed(); }
    });
    watch->setFuture(QtConcurrent::run(collect, m_root, cancel));
}
QVariantList QuickFileIndex::matches(const QString &query, const QVariantList &tabs) const
{
    if (m_root.isEmpty()) return {};
    QStringList buckets[7];
    QSet<QString> opened;
    const QString needle = query.trimmed();
    auto add = [&](const QString &path, bool open, bool tracked) {
        const auto name = QStringView(path).sliced(path.lastIndexOf('/') + 1);
        if (!path.contains(needle, Qt::CaseInsensitive)) return;
        const int match = name.startsWith(needle,Qt::CaseInsensitive) ? 0 : name.contains(needle,Qt::CaseInsensitive) ? 1 : 2;
        const int rank = open ? 0 : (tracked ? 1 : 4) + match;
        if (buckets[rank].size() < 80) buckets[rank].append(path);
    };
    for (const auto &tab : tabs) {
        const auto path = QDir(m_root).relativeFilePath(tab.toMap()["path"].toString());
        if (path.startsWith("../") || QDir::isAbsolutePath(path)) continue;
        opened.insert(path); add(path,true,false);
    }
    for (qsizetype i=0;i<m_files.size();++i) {
        const auto &path=m_files.at(i);
        if (!opened.contains(path)) add(path,false,i<m_tracked);
    }
    QVariantList result;
    for (const auto &bucket : buckets) for (const auto &path : bucket) {
        result.append(QVariantMap{{"title",path.section('/',-1)}, {"detail",path}, {"file",path}, {"root",m_root}});
        if (result.size() == 80) return result;
    }
    return result;
}
}
