#include "agent/codex/codex_account.h"
#include "agent/codex/thread_input.h"
#include "application/agent/agent_controller.h"
#include <QDir>
namespace exagent {
void AgentController::updateProfileBinding(ChatSession &session,
                                           const QString &id,
                                           const QVariantMap &source) {
  QVariantMap binding;
  for (const auto &key : {"threadId", "identity", "boundAccount", "boundCwd",
                          "responseId", "apiCwd"})
    if (source.contains(key))
      binding.insert(key, source.value(key));
  auto bindings = session.agent.value("profileBindings").toMap();
  bindings.insert(id, binding);
  session.agent.insert("profileBindings", bindings);
  if (accounts::reference(session.agent) == id)
    for (auto it = binding.begin(); it != binding.end(); ++it)
      session.agent.insert(it.key(), it.value());
}
bool AgentController::useProfile(const QString &id, const QString &model,
                                 bool automatic) {
  auto *s = find(m_selected);
  if(s && !s->history.value("terminalSource").toMap().isEmpty()) {emit failure(QStringLiteral("此会话使用终端账号，不能切换原生执行器。"));return false;}
  const auto p = m_profiles.profile(id);
  if (!s || !m_repository.writable() || !m_writable || p.isEmpty() ||
      p["enabled"] != 1 || model.size() > 128 ||
      (p["kind"] == 3 && model.trimmed().isEmpty()))
    return false;
  if (!busy() && !canTransferInput(s->id)) {
    emit failure(QStringLiteral("请先退出占有当前会话的终端。"));
    return false;
  }
  const auto before = s->agent;
  updateProfileBinding(*s, accounts::reference(s->agent), s->agent);
  for (const auto &key : {"threadId", "identity", "boundAccount", "boundCwd",
                          "responseId", "apiCwd"})
    s->agent.remove(key);
  s->agent.insert("accountProfile", id);
  s->agent.insert("account", p["kind"].toInt());
  s->agent.insert("model", model.trimmed());
  s->agent.insert("credentialFile", p["credentialFile"].toString());
  s->agent.insert("autoAccountSwitch", automatic ? 1 : 0);
  const auto b = s->agent.value("profileBindings").toMap().value(id).toMap();
  for (auto it = b.begin(); it != b.end(); ++it)
    s->agent.insert(it.key(), it.value());
  if (p["kind"] == 3)
    s->agent.insert("fullAccess", false);
  if (!save()) {
    s->agent = before;
    return false;
  }
  m_models.clear();
  emit changed();
  return true;
}
void AgentController::loginProfile(const QString &id) {
  const auto home = m_profiles.home(id);
  if (!m_writable || !m_repository.writable() || !m_profiles.writable() ||
      m_profiles.profile(id)["enabled"] != 1 || home.isEmpty() ||
      authenticating())
    return;
  for (const auto &a : m_active)
    if (a.snapshot["profileId"] == id) {
      emit failure(QStringLiteral("此账号仍有活动运行，请结束后再登录。"));
      return;
    }
  QString error;
  if (!codex::prepare(m_login, codex::System, &error, home)) {
    emit failure(error);
    return;
  }
  m_loginProfile = id;
  m_login.setWorkingDirectory(QDir::homePath());
  m_login.setStandardOutputFile(QProcess::nullDevice());
  m_login.setArguments({"-c", R"(cli_auth_credentials_store="file")", "login"});
  m_login.start();
}
} // namespace exagent
