#include "application/chat/chat_controller.h"
#include <QDir>
#include <QFileInfo>
#include <QUuid>
namespace exagent {
void ChatController::importTerminalConversation(const QVariantMap &row) {
  if (!m_storageWritable || m_preview)
    return;
  const int provider = row.value("agent") == "Pi"            ? 2
                       : row.value("agent") == "Claude Code" ? 3
                                                             : 0;
  const auto file = row.value("file").toString(),
             cwd = row.value("cwd").toString(), id = row.value("id").toString();
  if (!provider || QUuid(id).isNull() || !QDir::isAbsolutePath(file) ||
      !QDir::isAbsolutePath(cwd))
    return;
  QVariantMap source{
      {"provider", provider}, {"file", file}, {"cwd", cwd}, {"id", id}};
  QString target;
  for (const auto &s : m_sessions)
    if (s.history.value("terminalSource").toMap() == source) {
      target = s.id;
      break;
    }
  if (target.isEmpty()) {
    const auto previous = currentSessionId();
    // 全工作区列表可选其他目录；不把它错误归属到当前任务工作树。
    const auto workspace = QFileInfo(workspacePath()).canonicalFilePath() ==
                                   QFileInfo(cwd).canonicalFilePath()
                               ? currentWorkspaceId()
                               : QString();
    newGroupSession(currentProjectId(), workspace);
    if (m_current < 0 || currentSessionId() == previous)
      return;
    auto &s = m_sessions[m_current];
    s.title = row.value("agent").toString() + " · " + id.left(8);
    s.history.insert("terminalSource", source);
    s.agent.insert("account", 0);
    QString error;
    if (!saveNow(&error)) {
      setError(error);
      return;
    }
    target = s.id;
  } else
    selectSession(target);
  auto binding = source;
  binding.insert("source", source);
  binding.insert("conversationId", target);
  m_agent.syncTerminalConversation(binding);
  refreshSessions();
  emit sessionChanged();
}
} // namespace exagent
