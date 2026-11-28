#include "image_monitor.h"
#include "workspace_preview.h"
#include <QDir>
#include <QFileInfo>
namespace exagent {
WorkspaceImageMonitor::WorkspaceImageMonitor(QObject *parent)
    : QObject(parent) {
  m_delay.setSingleShot(true);
  m_delay.setInterval(150);
  connect(this, &WorkspaceImageMonitor::inputChanged, this,
          [this] { m_delay.start(0); });
  connect(&m_watch, &QFileSystemWatcher::fileChanged, this,
          [this] { m_delay.start(150); });
  connect(&m_watch, &QFileSystemWatcher::directoryChanged, this,
          [this] { m_delay.start(150); });
  connect(&m_delay, &QTimer::timeout, this, &WorkspaceImageMonitor::refresh);
}
WorkspaceImageMonitor::~WorkspaceImageMonitor() {
  WorkspaceImageProvider::release(m_info.value("url").toUrl());
}
void WorkspaceImageMonitor::refresh() {
  if (!m_watch.files().isEmpty())
    m_watch.removePaths(m_watch.files());
  if (!m_watch.directories().isEmpty())
    m_watch.removePaths(m_watch.directories());
  const auto old = m_info.value("url").toUrl();
  m_info.clear();
  m_error.clear();
  m_state = Idle;
  const auto parent = QFileInfo(m_path).absolutePath();
  // 父目录监听覆盖编辑器的原子替换、删除后重建；任何重建仍重新验证路径。
  if (!m_root.isEmpty() && QFileInfo(m_root).canonicalFilePath() == m_root &&
      QDir::cleanPath(m_path).startsWith(m_root.endsWith('/') ? m_root
                                                              : m_root + '/') &&
      (parent == m_root || parent.startsWith(m_root + '/')) &&
      QFileInfo(parent).canonicalFilePath() == parent) {
    m_watch.addPath(parent);
    if (QFileInfo::exists(m_path))
      m_watch.addPath(m_path);
    m_info = WorkspaceImageProvider::registerFile(m_path, m_root, &m_error);
    m_state = m_info.isEmpty() ? Unavailable : Ready;
  } else if (!m_path.isEmpty()) {
    m_error = QStringLiteral("图片不在当前工作区中。");
    m_state = Unavailable;
  }
  emit changed();
  WorkspaceImageProvider::release(old);
}
} // namespace exagent
