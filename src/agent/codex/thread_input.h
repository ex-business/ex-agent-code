#pragma once
#include <QFile>
#include <QLockFile>
#include <QVariantMap>
#include <memory>

namespace exagent::codex {
// 输入权跟随实际执行进程，与聊天窗口和终端页签的可见性无关。
#ifdef Q_OS_UNIX
using ThreadInputLock = QFile;
#else
using ThreadInputLock = QLockFile;
#endif
std::unique_ptr<ThreadInputLock> claimThread(const QString& thread);
bool threadAvailable(const QString& thread);
int terminalContinuation(const QVariantMap& spec);
}
