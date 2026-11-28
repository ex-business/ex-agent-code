#include "session_binding.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
static void initGuard() {
  static bool done = false;
  if (!done) {
    Q_INIT_RESOURCE(native_cli_guard);
    done = true;
  }
}
namespace exagent::cli {
QString installPiGuard() {
  initGuard();
  const auto dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/native-cli";
  const auto path = dir + "/pi-guard-v1.mjs";
  if (QFileInfo(dir).isSymLink() || !QDir().mkpath(dir) ||
      QFileInfo(path).isSymLink())
    return {};
  QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner);
  QFile resource(":/native-cli/pi_guard.mjs");
  QSaveFile file(path);
  if (!resource.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly))
    return {};
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  auto bytes = resource.readAll();
  if (file.write(bytes) != bytes.size() || !file.commit())
    return {};
  return path;
}
} // namespace exagent::cli
