#include "untracked_preview.h"
#include <QDir>
#include <QFile>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace exagent {
namespace {
QVariantMap unavailable() { return {{"message", QStringLiteral("此条目无法预览文件差异。")}}; }
#ifdef Q_OS_UNIX
struct Descriptor {
    int fd;
    ~Descriptor() { if (fd >= 0) ::close(fd); }
};

// 每一步均相对于已打开的目录句柄解析。被替换的目录或链接不能重定向后续读取。
bool descend(Descriptor &dir, const QString &name)
{
    if (name == "." || name == ".." || name.isEmpty()) return false;
    const int next = ::openat(dir.fd, QFile::encodeName(name).constData(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0) return false;
    ::close(dir.fd);
    dir.fd = next;
    return true;
}
#endif
}

QVariantMap previewUntracked(const QString &folder, const QString &path)
{
#ifdef Q_OS_UNIX
    if (!QDir::isAbsolutePath(folder) || QDir::isAbsolutePath(path)) return unavailable();
    Descriptor dir{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (dir.fd < 0) return unavailable();
    for (const auto &part : folder.split('/', Qt::SkipEmptyParts))
        if (!descend(dir, part)) return unavailable();
    auto parts = path.split('/');
    const QString leaf = parts.takeLast();
    if (leaf.isEmpty() || leaf == "." || leaf == "..") return unavailable();
    for (const auto &part : parts)
        if (!descend(dir, part)) return unavailable();
    const auto name = QFile::encodeName(leaf);
    struct stat info{};
    if (::fstatat(dir.fd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) return unavailable();

    QByteArray bytes;
    bool partial = false;
    if (S_ISLNK(info.st_mode)) {
        bytes.resize(16384);
        const auto size = ::readlinkat(dir.fd, name.constData(), bytes.data(), bytes.size());
        if (size < 0 || size == bytes.size()) return unavailable();
        bytes.truncate(size);
    } else {
        if (!S_ISREG(info.st_mode)) return unavailable();
        // O_NONBLOCK 防止路径在两步之间变为 FIFO；文件类型和上限按最终句柄核对。
        Descriptor file{::openat(dir.fd, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        if (file.fd < 0 || ::fstat(file.fd, &info) != 0 || !S_ISREG(info.st_mode)) return unavailable();
        if (info.st_size > 8*1024*1024)
            return {{"message", QStringLiteral("未跟踪文件超过 8 MiB 预览上限。")}};
        QFile input;
        if (!input.open(file.fd, QIODevice::ReadOnly)) return unavailable();
        constexpr int maxBytes = 128*1024;
        bytes = input.read(maxBytes + 1);
        if (input.error() != QFileDevice::NoError) return unavailable();
        partial = bytes.size() > maxBytes;
        bytes.truncate(qMin<qsizetype>(maxBytes, bytes.size()));
    }
    QStringList lines;
    const auto label = QString(path).replace('\n', "\\n").replace('\r', "\\r");
    lines << "new file mode " + QString(S_ISLNK(info.st_mode) ? "120000" : (info.st_mode & S_IXUSR) ? "100755" : "100644");
    if (bytes.first(qMin<qsizetype>(8000, bytes.size())).contains('\0'))
        lines << "Binary files /dev/null and b/" + label + " differ";
    else if (!bytes.isEmpty()) {
        lines << "--- /dev/null" << "+++ b/" + label;
        auto source = QString::fromUtf8(bytes).split('\n');
        if (bytes.endsWith('\n')) source.removeLast();
        if (!partial) lines << QString("@@ -0,0 +1,%1 @@").arg(source.size());
        partial |= source.size() > 3000;
        for (const auto &line : source.mid(0, 3000)) lines << "+" + line;
        if (!partial && !bytes.endsWith('\n')) lines << "\\ No newline at end of file";
        if (partial) lines << QStringLiteral("…");
    }
    QVariantMap data{{"lines",lines}, {"slow",partial}};
    if (partial) data.insert("message", QStringLiteral("差异过长，仅显示开头部分。"));
    return data;
#else
    Q_UNUSED(folder)
    Q_UNUSED(path)
    return unavailable();
#endif
}
}
