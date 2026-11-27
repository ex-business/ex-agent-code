#include "group_scan.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
namespace exagent::groups {
QVariantMap scanRepositories(const QString &root,
                             const std::shared_ptr<std::atomic_bool> &cancel) {
  QVariantList repos;
  QDirIterator dirs(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  int count = 0;
  while (dirs.hasNext()) {
    if (cancel->load())
      return {{"repos", repos}, {"error", QStringLiteral("扫描已取消。")}};
    if (++count > 256)
      return {
          {"error", QStringLiteral("子目录超过256个，请选择更小的父目录。")}};
    const auto path = QFileInfo(dirs.next()).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(path + "/.git").exists())
      continue;
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto &key : env.keys())
      if (key.startsWith("GIT_"))
        env.remove(key);
    process.setProcessEnvironment(env);
    process.start("git", {"-C", path, "rev-parse", "--show-toplevel"});
    if (!process.waitForStarted(1000) || !process.waitForFinished(2000)) {
      process.kill();
      process.waitForFinished(500);
      return {
          {"error",
           QStringLiteral("Git扫描超时或无法启动，请检查Git安装及目录权限。")}};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
      continue;
    // 只收录父目录的直接子仓库，不把属于祖先仓库的普通子目录误当成独立仓库。
    if (QFileInfo(QString::fromUtf8(process.readAllStandardOutput()).trimmed())
            .canonicalFilePath() != path)
      continue;
    if (repos.size() >= 64)
      return {{"error", QStringLiteral("最多导入64个子仓库。")}};
    repos.append(
        QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName()}});
  }
  return {{"repos", repos},
          {"error", repos.isEmpty() ? QStringLiteral("未找到直接子Git仓库。")
                                    : QString()}};
}
} // namespace exagent::groups
