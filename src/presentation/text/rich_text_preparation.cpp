#include "rich_text_preparation.h"
#include "markdown_formatter.h"
#include <QCoreApplication>
#include <QCache>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(preparationLog, "exagent.presentation.prepare")

namespace exagent {
namespace {
struct Input { QString source; QFont font; bool dark; int mode; };
struct Job { Input input; QPointer<RichTextPreparation> owner; quint64 generation; std::shared_ptr<std::atomic_bool> cancel; };
struct Cached { Input input; QVariantMap value; };
QString cacheKey(const Input &input) { return QString::number(qHash(input.source)) + ':' + QString::number(input.mode) + ':' + QString::number(input.dark) + ':' + input.font.key(); }
}
// 只在GUI线程管理队列；工作线程接收独立快照，不访问模型、QML或活控件。
class PreparationQueue : public QObject {
public:
    explicit PreparationQueue(QObject *parent) : QObject(parent) { m_pool.setMaxThreadCount(2); m_pool.setThreadPriority(QThread::LowPriority); }
    bool submit(Job job) {
        const auto key = cacheKey(job.input);
        if (const auto *cached = m_cache.object(key); cached && cached->input.source == job.input.source) {
            const auto result = cached->value;
            // submit已由对象的单次调度合并输入；缓存命中直接交付，避免再等一轮导致占位与正文交替重排。
            // generation与取消校验仍由apply负责，后台未命中的准备流程不变。
            if (job.owner && !job.cancel->load()) job.owner->apply(job.generation, result);
            return true;
        }
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            if (it->cancel->load() || !it->owner) { m_queuedBytes -= it->input.source.size() * 2; it = m_jobs.erase(it); }
            else ++it;
        }
        const auto bytes = job.input.source.size() * 2;
        if (m_jobs.size() >= 64 || m_queuedBytes + bytes > 16 * 1024 * 1024) return false;
        m_queuedBytes += bytes; m_jobs.append(std::move(job)); pump(); return true;
    }
    void retryWhenAvailable(RichTextPreparation *owner) { if (!m_waiting.contains(owner)) m_waiting.append(owner); }
private:
    void pump() {
        while (m_active < 2 && !m_jobs.isEmpty()) {
            auto job = m_jobs.takeFirst(); m_queuedBytes -= job.input.source.size() * 2;
            if (!job.owner || job.cancel->load()) continue;
            ++m_active; job.owner->setState(RichTextPreparation::Running);
            auto *watcher = new QFutureWatcher<QVariantMap>(this);
            connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, job] {
                --m_active;
                QVariantMap value;
                try { value = watcher->result(); } catch (...) { qCWarning(preparationLog) << "后台富文本准备失败，原始消息仍保留"; }
                watcher->deleteLater();
                if (!job.cancel->load() && job.owner) {
                    if (!value.isEmpty()) {
                        const auto cost = qMax(1, int((job.input.source.size() * 2 + value.value("html").toString().size() * 2 + 1023) / 1024));
                        m_cache.insert(cacheKey(job.input), new Cached{job.input, value}, cost);
                    }
                    job.owner->apply(job.generation, std::move(value));
                }
                pump();
                const auto waiting = m_waiting; m_waiting.clear();
                for (const auto &owner : waiting) if (owner && owner->m_waiting) owner->submit();
            });
            watcher->setFuture(QtConcurrent::run(&m_pool, [input = job.input, cancel = job.cancel] {
                if (cancel->load()) return QVariantMap{};
                // 每份QTextDocument在工作线程内独立创建和销毁，不跨线程搬动GUI资源。
                MarkdownFormatter formatter;
                auto result = input.mode == RichTextPreparation::User ? formatter.userPresentation(input.source, input.font)
                    : formatter.prosePresentation(input.source, input.dark);
                return cancel->load() ? QVariantMap{} : result;
            }));
        }
    }
    QThreadPool m_pool;
    int m_active = 0;
    qsizetype m_queuedBytes = 0;
    QList<Job> m_jobs;
    QList<QPointer<RichTextPreparation>> m_waiting;
    QCache<QString, Cached> m_cache{4096};
};
namespace {
PreparationQueue *queue() {
    // 由GUI线程第一次创建，挂在应用生命周期内；全窗口共享有界线程池与缓存。
    static QPointer<PreparationQueue> instance;
    if (!instance) instance = new PreparationQueue(QCoreApplication::instance());
    return instance;
}
}
RichTextPreparation::RichTextPreparation(QObject *parent) : QObject(parent)
{
    m_schedule.setSingleShot(true);
    connect(&m_schedule, &QTimer::timeout, this, &RichTextPreparation::submit);
}
RichTextPreparation::~RichTextPreparation() { if (m_cancel) m_cancel->store(true); }
void RichTextPreparation::setSource(const QString &source) { if (m_source != source) { m_source = source; schedule(); } }
void RichTextPreparation::setFont(const QFont &font) { if (m_font != font) { m_font = font; schedule(); } }
void RichTextPreparation::setDark(bool dark) { if (m_dark != dark) { m_dark = dark; schedule(); } }
void RichTextPreparation::setMode(int mode) { if (mode >= Prose && mode <= User && mode != m_mode) { m_mode = mode; schedule(); } }
void RichTextPreparation::setState(State state) { if (m_state != state) { m_state = state; emit stateChanged(); } }
void RichTextPreparation::schedule()
{
    ++m_generation; if (m_cancel) m_cancel->store(true);
    m_waiting = false;
    if (!m_value.isEmpty()) { emit layoutAboutToChange(); m_value.clear(); emit valueChanged(); emit layoutChanged(); }
    setState(m_source.isEmpty() ? Idle : Queued);
    m_schedule.start(0); emit inputChanged();
}
void RichTextPreparation::submit()
{
    if (m_source.isEmpty()) { m_waiting = false; setState(Idle); return; }
    if (m_source.size() > 8 * 1024 * 1024) { m_waiting = false; setState(Failed); return; }
    if (!m_cancel || m_cancel->load()) m_cancel = std::make_shared<std::atomic_bool>(false);
    m_waiting = false;
    if (!queue()->submit({{m_source, m_font, m_dark, m_mode}, this, m_generation, m_cancel})) {
        m_waiting = true; setState(Queued); queue()->retryWhenAvailable(this);
    }
}
void RichTextPreparation::apply(quint64 generation, QVariantMap value)
{
    if (generation != m_generation || !m_cancel || m_cancel->load()) return;
    emit layoutAboutToChange(); m_value = std::move(value); emit valueChanged();
    setState(m_value.isEmpty() ? Failed : Ready); emit layoutChanged();
}
}
