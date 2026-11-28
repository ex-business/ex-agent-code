#include "history_reader.h"
#include <QHash>
#include <QReadWriteLock>
namespace exagent::terminalchat {
namespace {
QReadWriteLock lock;
QHash<int, Reader> readers{
    {Codex, readCodex}, {Pi, readJsonl}, {ClaudeCode, readJsonl}};
} // namespace
bool registerReader(int provider, Reader reader) {
  QWriteLocker guard(&lock);
  if (provider <= ClaudeCode || !reader || readers.contains(provider))
    return false;
  readers.insert(provider, std::move(reader));
  return true;
}
Snapshot read(const QVariantMap &binding) {
  Reader reader;
  {
    QReadLocker guard(&lock);
    reader = readers.value(binding.value("provider", Codex).toInt());
  }
  if (!reader)
    return {{}, QStringLiteral("没有可用的终端历史适配插件。")};
  return reader(binding);
}
} // namespace exagent::terminalchat
