#include "file_tools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace exagent::openai {
QJsonArray fileTools(bool write) {
  QJsonArray tools;
  for (const QString &name :
       {QStringLiteral("read_file"), QStringLiteral("write_file")}) {
    if (name == "write_file" && !write)
      continue;
    QJsonObject props{
        {"path", QJsonObject{{"type", "string"},
                             {"description", "Workspace-relative file path"}}}};
    QJsonArray required{"path"};
    if (name == "write_file") {
      props.insert("content", QJsonObject{{"type", "string"}});
      required.append("content");
    }
    tools.append(QJsonObject{
        {"type", "function"},
        {"name", name},
        {"description",
         name == "read_file"
             ? "Read a UTF-8 file in the workspace (maximum 64 KiB)."
             : "Replace a UTF-8 file in the workspace (maximum 64 KiB), "
               "subject to user approval."},
        {"strict", true},
        {"parameters", QJsonObject{{"type", "object"},
                                   {"properties", props},
                                   {"required", required},
                                   {"additionalProperties", false}}}});
  }
  return tools;
}

FileResult fileTool(const QString &root, const QString &secretFile,
                    const QString &name, const QJsonObject &a, bool write) {
  const auto rel = a["path"].toString();
  if ((name != "read_file" && name != "write_file") || !a["path"].isString() ||
      rel.isEmpty() || QDir::isAbsolutePath(rel))
    return {false, QStringLiteral("拒绝：工具或路径无效。")};
  const auto clean = QDir::cleanPath(rel);
  if (clean == ".." || clean.startsWith("../"))
    return {false, QStringLiteral("拒绝：路径超出工作区。")};
  for (const auto &part : clean.split('/')) {
    const auto lower = part.toLower();
    if (lower == ".git" || lower == ".codex" || lower.startsWith(".env"))
      return {false, QStringLiteral("拒绝：受保护文件。")};
  }
#ifdef Q_OS_UNIX
  // 每段目录均固定在已打开fd上并拒绝符号链接，不能在检查后用路径重开。
  struct Fd {
    int n = -1;
    ~Fd() {
      if (n >= 0)
        ::close(n);
    }
  } dir;
  dir.n = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  auto descend = [&](const QStringList &parts) {
    for (const auto &part : parts) {
      const auto bytes = QFile::encodeName(part);
      const int next =
          ::openat(dir.n, bytes.constData(),
                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (next < 0)
        return false;
      ::close(dir.n);
      dir.n = next;
    }
    return true;
  };
  if (!QDir::isAbsolutePath(root) || dir.n < 0 ||
      !descend(root.split('/', Qt::SkipEmptyParts)))
    return {false, QStringLiteral("拒绝：工作区目录无效。")};
  auto parts = clean.split('/');
  const auto filename = QFile::encodeName(parts.takeLast());
  if (!descend(parts))
    return {false, QStringLiteral("拒绝：父目录无效或包含符号链接。")};
  struct stat current {};
  const bool exists = ::fstatat(dir.n, filename.constData(), &current,
                                AT_SYMLINK_NOFOLLOW) == 0;
  struct stat secret {};
  const bool hasSecret =
      !secretFile.isEmpty() &&
      ::stat(QFile::encodeName(secretFile).constData(), &secret) == 0;
  auto protectedFile = [&](const struct stat &st) {
    return !S_ISREG(st.st_mode) || st.st_nlink > 1 ||
           (hasSecret && st.st_dev == secret.st_dev &&
            st.st_ino == secret.st_ino);
  };
  if (exists && protectedFile(current))
    return {false, QStringLiteral("拒绝：链接、凭据或非普通文件。")};
  if (name == "read_file") {
    Fd file;
    file.n = ::openat(dir.n, filename.constData(),
                      O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    struct stat opened {};
    if (file.n < 0 || ::fstat(file.n, &opened) != 0 || protectedFile(opened) ||
        opened.st_size > 65536)
      return {false, QStringLiteral("拒绝：文件不存在、不安全或超过 64 KiB。")};
    QByteArray bytes;
    char buffer[8192];
    while (bytes.size() <= 65536) {
      const auto n = ::read(file.n, buffer, sizeof(buffer));
      if (n < 0)
        return {false, QStringLiteral("读取失败。")};
      if (n == 0)
        break;
      bytes.append(buffer, n);
    }
    if (bytes.size() > 65536 || bytes.contains('\0'))
      return {false, QStringLiteral("拒绝：文件过大或不是文本。")};
    return {true, QString::fromUtf8(bytes)};
  }
  if (!write || !a["content"].isString())
    return {false, QStringLiteral("拒绝：没有工作区写入权限或参数无效。")};
  const auto bytes = a["content"].toString().toUtf8();
  if (bytes.size() > 65536)
    return {false, QStringLiteral("拒绝：内容超过 64 KiB。")};
  const auto tmp = QFile::encodeName(
      ".ex-write-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  Fd out;
  out.n = ::openat(dir.n, tmp.constData(),
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (out.n < 0)
    return {false, QStringLiteral("无法创建原子写入文件。")};
  qsizetype offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    const auto n =
        ::write(out.n, bytes.constData() + offset, bytes.size() - offset);
    if (n <= 0) {
      ok = false;
      break;
    }
    offset += n;
  }
  if (ok && exists)
    ok = ::fchmod(out.n, current.st_mode & 0777) == 0;
  if (ok)
    ok = ::fsync(out.n) == 0;
  if (ok)
    ok = ::renameat(dir.n, tmp.constData(), dir.n, filename.constData()) == 0;
  // 只移除本次写入的临时占位，不删除用户文件；目标文件通过rename原子替换。
  if (!ok)
    ::unlinkat(dir.n, tmp.constData(), 0);
  return {ok, ok ? QStringLiteral("写入成功。") : QStringLiteral("写入失败。")};
#else
  Q_UNUSED(root);
  Q_UNUSED(secretFile);
  Q_UNUSED(write);
  return {false, QStringLiteral("此平台尚未提供安全的文件工具实现。")};
#endif
}
} // namespace exagent::openai
