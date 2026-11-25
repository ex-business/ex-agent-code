#include "application/chat/chat_controller.h"
#include "design_selection.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace exagent {
QVariantMap ChatController::designSelection() const
{
    return m_current < 0 || m_preview ? QVariantMap{} : design::pending(m_sessions[m_current]).value("context").toMap();
}
bool ChatController::designCaptureBusy() const
{
    return !m_preview && m_designCaptures.contains(currentSessionId());
}
void ChatController::clearDesignSelection()
{
    if (!m_storageWritable || m_preview || m_current < 0) return;
    m_designCaptures.remove(currentSessionId());
    design::setPending(m_sessions[m_current], {});
    emit designSelectionChanged();
    flush();
}
void ChatController::captureDesignElement(const QString &session, const QVariantMap &details, const QImage &surface)
{
    if (!m_storageWritable || m_preview || m_current < 0 || session != currentSessionId() || currentWorkspaceId().isEmpty()) return;
    const QString directory = m_repository.sessionDirectory(currentProjectId(), session);
    const QString ticket = createId();
    m_designCaptures.insert(session, ticket);
    emit designSelectionChanged();
    auto *watcher = new QFutureWatcher<design::SelectionResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, session, ticket] {
        const auto result = watcher->result();
        watcher->deleteLater();
        // 会话切换、再次点选和移除附件可能与编码并发，只有原草稿的最新结果可以发布。
        if (m_designCaptures.value(session) != ticket) return;
        m_designCaptures.remove(session);
        const bool selected = !m_preview && currentSessionId() == session;
        if (!result.error.isEmpty()) {
            if (selected) { emit designSelectionChanged(); setError(result.error); }
            return;
        }
        for (auto &entry : m_sessions) {
            if (entry.id != session) continue;
            design::setPending(entry,result.selection);
            flush();
            if (selected) emit designSelectionChanged();
            break;
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_exporter,[details,surface,directory] {
        return design::capture(details,surface,directory);
    }));
}
}
