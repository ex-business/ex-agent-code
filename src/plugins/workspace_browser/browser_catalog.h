#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 配置身份使用工作区 ID 与独立 UUID，显示名称和仓库内文件均不能指定 Chromium 的存储路径。
class BrowserCatalog : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantList profiles(const QString &workspace) const;
    Q_INVOKABLE QString selected(const QString &workspace) const;
    Q_INVOKABLE QString create(const QString &workspace, const QString &name);
    Q_INVOKABLE bool select(const QString &workspace, const QString &profile);
    Q_INVOKABLE QString partition(const QString &workspace, const QString &profile) const;
    Q_INVOKABLE QVariantMap restore(const QString &partition) const;
    Q_INVOKABLE void save(const QString &partition, const QStringList &urls, int active);
    Q_INVOKABLE QUrl address(const QString &input) const;
    Q_INVOKABLE bool allowed(const QUrl &url) const;
};
}
