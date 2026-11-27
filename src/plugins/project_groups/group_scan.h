#pragma once
#include <QVariantMap>
#include <atomic>
#include <memory>
namespace exagent::groups {
QVariantMap scanRepositories(const QString &root,
                             const std::shared_ptr<std::atomic_bool> &cancel);
}
