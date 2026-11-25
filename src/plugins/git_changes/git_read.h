#pragma once
#include <QStringList>
#include <QProcessEnvironment>
#include <atomic>
#include <memory>

namespace exagent::gitread {
using Cancel = std::shared_ptr<std::atomic_bool>;
struct Output {
    QByteArray data;
    QString error;
    bool truncated = false;
};

QProcessEnvironment workspaceEnvironment();
Output runGit(const QString &folder, QStringList args, const Cancel &cancel, int limit);
}
