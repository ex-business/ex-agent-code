#include "rich_text_preparation.h"
#include "markdown_formatter.h"
#include <QCoreApplication>
#include <QCache>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <QLoggingCategory>
#include <QTextDocument>
#include <QtMath>
#include <utility>
Q_LOGGING_CATEGORY(preparationLog, "exagent.presentation.prepare")

namespace exagent {
namespace {
struct Input { QString source; QFont font; bool dark; int mode; qreal width; };
struct Job { Input input; QPointer<RichTextPreparation> owner; quint64 generation; std::shared_ptr<std::atomic_bool> cancel; };
struct Cached { Input input; QVariantMap value; };
QString cacheKey(const Input &input) { return QString::number(qHash(input.source)) + ':' + QString::number(input.mode) + ':' + QString::number(input.dark) + ':' + input.font.key() + ':' + QString::number(input.width, 'g', 17); }
}
// 只在GUI线程管理队列；工作线程接收独立快照，不访问模型、QML或活控件。
class PreparationQueue : public QObject {
public:
    explicit PreparationQueue(QObject *parent) : QObject(parent) {
        m_pool.setMaxThreadCount(2); m_pool.setThreadPriority(QThread::LowPriority);
        m_pumpTimer.setSingleShot(true);
        connect(&m_pumpTimer, &QTimer::timeout, this, [this] {
            pump();
            // 方向变化可能释放队列容量，即使没有活动线程完成也要唤醒可见任务。
            const auto waiting = m_waiting; m_waiting.clear();
            for (const auto &owner : waiting) {
                if (!owner || !owner->m_waiting) continue;
                if (owner->m_priority >= 0 && !owner->m_deferUncached) owner->submit();
                else retryWhenAvailable(owner);
            }
        });
    }
    void requestPump() { if (!m_pumpTimer.isActive()) m_pumpTimer.start(0); }
    QVariantMap layoutHint(const Input &input) const {
        const auto *cached = m_cache.object(cacheKey(input));
        if (!cached || cached->input.source != input.source) return {};
        return {{"height", cached->value.value("layoutHeight")}, {"bubbleWidth", cached->value.value("bubbleWidth")}};
    }
    bool submit(Job job) {
        const auto key = cacheKey(job.input);
        if (const auto *cached = m_cache.object(key); cached && cached->input.source == job.input.source) {
            const auto result = cached->value;
            // submit已由对象的单次调度合并输入；缓存命中直接交付，避免再等一轮导致占位与正文交替重排。
            // generation与取消校验仍由apply负责，后台未命中的准备流程不变。
            if (job.owner && !job.cancel->load()) job.owner->apply(job.generation, result, true);
            return true;
        }
        // 快速滚动优先命中已有准备缓存；未命中先保留轻量预览，停下后再解析。
        if (job.owner && (job.owner->m_deferUncached || job.owner->m_priority < 0)) return false;
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
            // 每次取任务都读取当前优先级，反向时不必取消再创建同一份准备任务。
            int best = -1;
            int priority = -1;
            for (int i = 0; i < m_jobs.size();) {
                const auto &candidate = m_jobs.at(i);
                if (!candidate.owner || candidate.cancel->load() || candidate.owner->m_priority < 0 || candidate.owner->m_deferUncached) {
                    if (candidate.owner && !candidate.cancel->load()) {
                        candidate.owner->m_waiting = true;
                        retryWhenAvailable(candidate.owner);
                    }
                    m_queuedBytes -= candidate.input.source.size() * 2;
                    m_jobs.removeAt(i);
                    continue;
                }
                ++i;
            }
            for (int i = 0; i < m_jobs.size(); ++i) {
                if (m_jobs.at(i).owner->m_priority > priority) { best = i; priority = m_jobs.at(i).owner->m_priority; }
            }
            if (best < 0) break;
            auto job = m_jobs.takeAt(best); m_queuedBytes -= job.input.source.size() * 2;
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
                if (!cancel->load() && input.width > 0) {
                    // 与最终TextEdit使用相同HTML、字体和可用宽度，先在独立文档里完成换行测量。
                    // 高度随准备结果进入有界缓存，不用字符数猜测已访问消息的高度。
                    QTextDocument document;
                    document.setDefaultFont(input.font);
                    document.setDocumentMargin(0);
                    document.setHtml(result.value("html").toString());
                    qreal textWidth = input.width;
                    if (input.mode == RichTextPreparation::User) {
                        const qreal bubbleWidth = qMin(input.width, qMax(42.0, qCeil(result.value("width").toReal()) + 36.0));
                        result.insert("bubbleWidth", bubbleWidth);
                        textWidth = qMax(1.0, bubbleWidth - 32.0);
                    }
                    document.setTextWidth(textWidth);
                    result.insert("layoutHeight", qCeil(document.size().height()));
                }
                return cancel->load() ? QVariantMap{} : result;
            }));
        }
    }
    QTimer m_pumpTimer;
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
QVariantMap RichTextPreparation::layoutHint() const
{
    if (m_source.isEmpty() || m_layoutWidth <= 0) return {};
    return m_layoutHint;
}
void RichTextPreparation::setPreparationPriority(int priority)
{
    if (m_priority == priority) return;
    m_priority = priority; emit inputChanged();
    // 提升只唤醒排队任务，不重建generation或清空已准备正文；回收中的任务保持暂停。
    if (priority >= 0 && m_waiting) m_schedule.start(0);
    queue()->requestPump();
}
void RichTextPreparation::setLayoutWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_layoutWidth, width)) return;
    m_layoutWidth = width; schedule();
}
void RichTextPreparation::setHoldLayout(bool hold)
{
    if (m_holdLayout == hold) return;
    m_holdLayout = hold; emit inputChanged();
    // 手势结束后一次发布当前消息的测量结果；旧代次在schedule中清空，不能串到复用后的消息。
    if (!hold && !m_pendingValue.isEmpty()) apply(m_generation, std::exchange(m_pendingValue, {}));
}
void RichTextPreparation::setDeferUncached(bool defer)
{
    if (m_deferUncached == defer) return;
    m_deferUncached = defer;
    emit inputChanged();
    // 恢复的是当前generation，旧消息任务仍由既有取消与代次校验丢弃。
    if (!defer && m_waiting) m_schedule.start(0);
    queue()->requestPump();
}
void RichTextPreparation::setSource(const QString &source) { if (m_source != source) { m_source = source; schedule(); } }
void RichTextPreparation::setFont(const QFont &font) { if (m_font != font) { m_font = font; schedule(); } }
void RichTextPreparation::setDark(bool dark) { if (m_dark != dark) { m_dark = dark; schedule(); } }
void RichTextPreparation::setMode(int mode) { if (mode >= Prose && mode <= User && mode != m_mode) { m_mode = mode; schedule(); } }
void RichTextPreparation::setState(State state) { if (m_state != state) { m_state = state; emit stateChanged(); } }
void RichTextPreparation::schedule()
{
    m_pendingValue.clear();
    m_layoutHint = m_source.isEmpty() ? QVariantMap{} : queue()->layoutHint({m_source, m_font, m_dark, m_mode, m_layoutWidth});
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
    if (!queue()->submit({{m_source, m_font, m_dark, m_mode, m_layoutWidth}, this, m_generation, m_cancel})) {
        m_waiting = true; setState(Queued); queue()->retryWhenAvailable(this);
    }
}
void RichTextPreparation::apply(quint64 generation, QVariantMap value, bool cached)
{
    if (generation != m_generation || !m_cancel || m_cancel->load()) return;
    // 运动期间缓存结果但不发布高度变化，避免每完成一个后台任务就把整页重新挤动。
    // 同宽同字体的缓存已经有最终高度；重新进入视口时直接恢复，避免先闪现 Markdown 原文。
    if (m_holdLayout && !(cached && m_priority >= 0) && !value.isEmpty()) { m_pendingValue = std::move(value); return; }
    emit layoutAboutToChange(); m_value = std::move(value); emit valueChanged();
    emit inputChanged();
    setState(m_value.isEmpty() ? Failed : Ready); emit layoutChanged();
}
}
