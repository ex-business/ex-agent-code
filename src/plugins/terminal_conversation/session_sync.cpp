#include "agent/codex/thread_input.h"
#include "application/agent/agent_controller.h"
#include "history_reader.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
namespace exagent {
QString AgentController::terminalAgent() const {
  const auto *s = find(m_selected);
  const auto provider =
      s ? s->history.value("terminalSource").toMap().value("provider").toInt()
        : 0;
  return provider == 2   ? QStringLiteral("Pi")
         : provider == 3 ? QStringLiteral("Claude Code")
                         : QString();
}
void AgentController::syncTerminalConversation(const QVariantMap &binding) {
  const auto session = binding.value("conversationId").toString();
  auto *s = find(session);
  const auto profile = binding.value("profileId").toString();
  const bool external = binding.value("provider", 1).toInt() != 1;
  if (!s || !m_repository.writable() || !accountStorageWritable ||
      !accountStorageWritable())
    return;
  if (m_active.contains(session) || m_syncing.contains(session) ||
      m_syncing.size() >= 2) {
    // 退出事件不能因聊天暂忙而丢失；每会话只保留一个有界重试。
    if (!m_pendingTerminalSync.contains(session)) {
      m_pendingTerminalSync.insert(session);
      QTimer::singleShot(1000, this, [this, session, binding] {
        m_pendingTerminalSync.remove(session);
        syncTerminalConversation(binding);
      });
    }
    return;
  }
  auto expected =
      s->agent.value("profileBindings").toMap().value(profile).toMap();
  if (expected.isEmpty() && profile == accounts::reference(s->agent))
    expected = s->agent;
  if ((external && s->history.value("terminalSource").toMap() !=
                       binding.value("source").toMap()) ||
      (!external &&
       (profile.isEmpty() ||
        expected.value("threadId") != binding.value("threadId") ||
        expected.value("identity") != binding.value("identity") ||
        expected.value("boundCwd") != binding.value("boundCwd") ||
        expected.value("boundAccount") != binding.value("boundAccount")))) {
    emit failure(QStringLiteral("终端绑定已变化，未覆盖当前会话。"));
    return;
  }
  QVariantMap request = binding;
  if (external) {
    const auto source = s->history.value("terminalSource").toMap();
    for (const auto &key : {"provider", "file", "cwd", "id"}) {
      if (binding.value(key) != source.value(key)) {
        emit failure(QStringLiteral("终端历史来源不匹配。"));
        return;
      }
    }
  }
  QStringList known;
  for (const auto &m : s->messages) {
    if (m.role == MessageRole::Notice &&
        m.activity.value("threadId") == binding.value("threadId"))
      known.append(m.activity.value("turnId").toString());
  }
  request.insert("knownTurns", known);
  request.insert("accountHome", m_profiles.home(profile));
  m_syncing.insert(session);
  emit changed();
  auto *watcher = new QFutureWatcher<terminalchat::Snapshot>(this);
  connect(
      watcher, &QFutureWatcher<terminalchat::Snapshot>::finished, this,
      [this, watcher, session, binding, profile, external] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_syncing.remove(session);
        emit changed();
        auto *s = find(session);
        if (!s || !m_repository.writable() || !accountStorageWritable ||
            !accountStorageWritable() || m_active.contains(session))
          return;
        if (!result.error.isEmpty()) {
          emit failure(result.error);
          return;
        }
        auto current =
            s->agent.value("profileBindings").toMap().value(profile).toMap();
        if (current.isEmpty() && profile == accounts::reference(s->agent))
          current = s->agent;
        if ((external && s->history.value("terminalSource").toMap() !=
                             binding.value("source").toMap()) ||
            (!external &&
             (current.value("threadId") != binding.value("threadId") ||
              current.value("identity") != binding.value("identity"))))
          return;
        QSet<QString> resultIds, existingIds;
        for (const auto &row : result.messages)
          resultIds.insert(row.id);
        for (const auto &old : s->messages) {
          existingIds.insert(old.id);
          if (external && old.activity.contains("terminalEntry") &&
              !resultIds.contains(old.id)) {
            emit failure(QStringLiteral(
                "历史活动分支已改变，保留原聊天；请先核对分支再同步。"));
            return;
          }
        }
        bool changedRows = false;
        QHash<QString, int> anchors;
        for (int i = 0; i < s->messages.size(); ++i) {
          const auto &row = s->messages[i];
          if (row.role == MessageRole::Notice &&
              !row.activity.value("turnId").toString().isEmpty()) {
            int at = i;
            if (i > 0 && s->messages[i - 1].role == MessageRole::User)
              at = i - 1;
            anchors.insert(row.activity.value("turnId").toString(), at);
          }
        }
        QHash<int, QList<ChatMessage>> batches;
        QHash<QString, int> positions;
        QHash<QString, ChatMessage> replacements;
        for (int i = 0; i < s->messages.size(); ++i)
          positions.insert(s->messages[i].id, i);
        // 使用哈希去重并整批发布，避免长历史在GUI线程逐条扫描和重新排版。
        for (const auto &row : result.messages)
          if (!existingIds.contains(row.id)) {
            const auto anchor =
                row.activity.value("terminalBeforeTurn").toString();
            batches[anchors.value(anchor, s->messages.size())].append(row);
            existingIds.insert(row.id);
            changedRows = true;
          } else if (external && positions.contains(row.id)) {
            const auto &old = s->messages[positions.value(row.id)];
            if (old.content != row.content || old.status != row.status ||
                old.role != row.role || old.startsTurn != row.startsTurn) {
              auto updated = row;
              updated.createdAt = old.createdAt;
              updated.originalTimestamp = old.originalTimestamp;
              updated.expanded = old.expanded;
              replacements.insert(row.id, updated);
              changedRows = true;
            }
          }
        if (changedRows) {
          // 延迟回填也放在后续原生回合之前，避免退出后快速发送造成时间顺序颠倒。
          QList<ChatMessage> merged;
          merged.reserve(s->messages.size() + result.messages.size());
          for (int i = 0; i <= s->messages.size(); ++i) {
            merged.append(batches.value(i));
            if (i < s->messages.size())
              merged.append(
                  replacements.value(s->messages[i].id, s->messages[i]));
          }
          s->messages = std::move(merged);
          s->updatedAt = QDateTime::currentDateTimeUtc();
          emit terminalHistoryChanged(session);
          if (save())
            emit saved();
        }
      });
  watcher->setFuture(QtConcurrent::run(terminalchat::read, request));
}
} // namespace exagent
