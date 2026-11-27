#include "credentials.h"
#include <QFile>
#include <QFileInfo>
namespace exagent::openai {
QByteArray credential(const QString &path, QString *error) {
  QByteArray key;
  if (path.isEmpty())
    key = qgetenv("OPENAI_API_KEY").trimmed();
  else {
    const QFileInfo info(path);
    // 密钥仅从用户指定的私有文件读取，不能随工作目录自动发现或跟随符号链接。
    if (!info.isAbsolute() || info.isSymLink() || !info.isFile() ||
        info.size() > 65536 ||
        (info.permissions() &
         (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
          QFileDevice::ReadOther | QFileDevice::WriteOther))) {
      *error = QStringLiteral(
          "API 凭据文件必须是绝对路径的私有普通文件（建议权限 600）。");
      return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      *error = QStringLiteral("无法读取 API 凭据文件。");
      return {};
    }
    auto bytes = f.readAll();
    for (auto line : bytes.split('\n')) {
      line = line.trimmed();
      if (line.startsWith("OPENAI_API_KEY=")) {
        key = line.mid(15).trimmed();
        if (key.size() > 1 && ((key.front() == '"' && key.back() == '"') ||
                               (key.front() == '\'' && key.back() == '\'')))
          key = key.mid(1, key.size() - 2);
        break;
      }
    }
    bytes.fill(0);
  }
  if (key.size() < 20 || key.size() > 4096 || key.contains('\r') ||
      key.contains('\n')) {
    key.fill(0);
    *error =
        QStringLiteral("未找到有效 OPENAI_API_KEY，请检查凭据文件或启动环境。");
    return {};
  }
  return key;
}
} // namespace exagent::openai
