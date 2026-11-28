#include "plugins/session_history/history_provider.h"
#include "plugins/session_history/session_console.h"
#include "plugins/session_history/terminal_backend.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
namespace exagent {
void SessionConsole::continueCodex(const QVariantMap &binding) {
  if (binding.value("provider", 1).toInt() != 1) {
    const int provider = binding.value("provider").toInt();
    if (provider != 2 && provider != 3) {
      fail(QStringLiteral("终端续接插件不可用。"));
      return;
    }
    const auto file = binding.value("file").toString(),
               id = binding.value("id").toString(),
               cwd = binding.value("cwd").toString();
    QVariantMap descriptor;
    for (const auto &v : discoverSessions(cwd)) {
      const auto row = v.toMap();
      if (row.value("file") == file && row.value("id") == id &&
          row.value("agent") == QString(provider == 2 ? "Pi" : "Claude Code"))
        descriptor = row;
    }
    const auto program = binding.value("native")
                             .toMap()
                             .value("program", descriptor.value("program"))
                             .toString();
    if (descriptor.isEmpty() || !QFileInfo(program).isExecutable()) {
      fail(QStringLiteral("对应CLI或历史绑定不可用。"));
      return;
    }
    for (int i = 0; i < m_tabs.size(); ++i) {
      const auto row = m_tabs[i].toMap();
      if (row.value("origin") != file)
        continue;
      const auto snapshot = terminalSnapshot(row.value("id").toString(), false);
      if (snapshot.value("observed").toInt() != 1) {
        fail(QStringLiteral("已有终端状态暂时无法核对，未创建重复实例。"));
        return;
      }
      if (snapshot.value("state").toInt() == Running) {
        select(i);
        return;
      }
    }
    const auto previous = m_tabs.size();
    auto launch = binding;
    launch.insert("program", program);
    start(descriptor.value("agent").toString(), cwd,
          {QCoreApplication::applicationFilePath(), "--continue-agent-terminal",
           QString::fromUtf8(QJsonDocument::fromVariant(launch)
                                 .toJson(QJsonDocument::Compact)
                                 .toBase64())},
          file, {}, binding.value("conversationId").toString());
    if (m_tabs.size() > previous) {
      auto row = current();
      row.insert("conversationBinding", binding);
      m_tabs[m_selected] = row;
      save();
    }
    return;
  }
  const auto thread = binding.value("threadId").toString();
  if (QUuid(thread).isNull()) {
    fail(QStringLiteral("当前会话没有可恢复的 Codex 线程。"));
    return;
  }
  const auto origin = "codex:" + thread;
  for (int i = 0; i < m_tabs.size(); ++i) {
    const auto row = m_tabs[i].toMap();
    if (row.value("origin") != origin)
      continue;
    const auto snapshot = terminalSnapshot(row.value("id").toString(), false);
    if (snapshot.value("observed").toInt() != 1) {
      fail(QStringLiteral("已有终端状态暂时无法核对，未创建重复实例。"));
      return;
    }
    if (snapshot.value("state").toInt() == Running) {
      select(i);
      return;
    }
  }
  const auto previous = m_tabs.size();
  start("Codex · " + thread.left(8), binding.value("boundCwd").toString(),
        {QCoreApplication::applicationFilePath(), "--continue-codex-terminal",
         QString::fromLatin1(QJsonDocument::fromVariant(binding)
                                 .toJson(QJsonDocument::Compact)
                                 .toBase64())},
        origin, {}, binding.value("conversationId").toString());
  if (m_tabs.size() > previous &&
      current().value("conversationId") == binding.value("conversationId")) {
    auto row = m_tabs[m_selected].toMap();
    row.insert("conversationBinding", binding);
    m_tabs[m_selected] = row;
    save();
  }
}
} // namespace exagent
