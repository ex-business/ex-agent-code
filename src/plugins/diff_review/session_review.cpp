#include "application/chat/chat_controller.h"
#include <QFileInfo>

namespace exagent {
QVariantList ChatController::reviewNotes() const
{
    if (m_preview || m_current < 0) return {};
    QVariantList visible;
    const auto folder = QFileInfo(workspacePath()).canonicalFilePath();
    for (const auto &v : m_sessions.at(m_current).history.value("reviewNotes").toList()) {
        const auto n = v.toMap();
        if (n.value("folder") == folder && n.value("session") == currentSessionId()) visible << v;
    }
    return visible;
}

QVariantMap ChatController::reviewOrigin(const QVariantMap &anchor) const
{
    if (m_preview || m_current < 0 || anchor.value("signature").toString().isEmpty()
        || (anchor.value("oldLine").toInt() && anchor.value("newLine").toInt())) return {};
    const auto &session = m_sessions.at(m_current);
    const auto path = anchor.value("path").toString();
    QVariantMap match;
    for (const auto &row : session.messages) {
        if (row.status != MessageStatus::Complete || row.role != MessageRole::Tool || row.activity.value("serviceId") != "codex-agent"
            || row.activity.value("patchWorkspace") != anchor.value("folder")) continue;
        const auto proofs = row.activity.value("filePatches").toMap();
        if (proofs.value(path) != anchor.value("signature")) continue;
        // 多次运行产生相同补丁时，无法唯一定位作者，宁可不给出来源。
        if (!match.isEmpty()) return {};
        match = {{"messageId", row.id}, {"runId", row.activity.value("runId")}};
    }
    return match;
}

bool ChatController::saveReviewNote(const QString &sessionId, QVariantMap anchor, const QString &comment)
{
    if (!m_storageWritable || m_preview || m_current < 0 || currentSessionId() != sessionId
        || workspacePath().isEmpty() || anchor.value("folder") != QFileInfo(workspacePath()).canonicalFilePath()
        || anchor.value("path").toString().isEmpty() || anchor.value("snapshot").toString().size() != 64
        || (!anchor.value("oldLine").toInt() && !anchor.value("newLine").toInt())
        || comment.trimmed().isEmpty() || comment.size() > 1500) return false;
    auto &history = m_sessions[m_current].history;
    const auto before = history;
    auto notes = history.value("reviewNotes").toList();
    if (notes.size() >= 128) return false;
    anchor.insert("id", createId());
    anchor.insert("session", sessionId);
    anchor.insert("comment", comment.trimmed());
    anchor.insert("origin", reviewOrigin(anchor));
    notes.append(anchor);
    history.insert("reviewNotes", notes);
    QString error;
    if (!m_repository.save(m_projects.catalog,m_sessions,&error)) {
        history = before;
        setError(error);
        return false;
    }
    emit reviewNotesChanged();
    return true;
}

bool ChatController::sendReviewNote(const QString &id)
{
    for (const auto &value : reviewNotes()) {
        const auto n = value.toMap();
        if (n.value("id") != id) continue;
        const auto oldLine = n.value("oldLine").toInt(), newLine = n.value("newLine").toInt();
        QString location = n.value("path").toString();
        if (oldLine) location += QStringLiteral(" · 旧行 %1").arg(oldLine);
        if (newLine) location += QStringLiteral(" · 新行 %1").arg(newLine);
        auto versions = n.value("revisions").toStringList();
        if (n.value("scope").toInt() != 2)
            versions << (n.value("area").toInt() == 2 ? QStringLiteral("暂存区") : QStringLiteral("工作区"));
        const auto prompt = QStringLiteral("行级批注 · %1\n版本 %2\n快照 %3\n原文：%4\n\n%5")
            .arg(location, versions.join(" → "),
                 n.value("snapshot").toString(), n.value("source").toString(), n.value("comment").toString());
        return m_agent.submit(prompt, workspacePath(), n);
    }
    return false;
}
}
