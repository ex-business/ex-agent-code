#pragma once
#include <QObject>
#include <QFileInfo>
#include <functional>
#include <QVariantMap>
namespace exagent {
// 提供者完成目录准备后回调宿主；会话与目录的归属统一由宿主管理。
class TaskWorkspaceProvider : public QObject {
public:
    using QObject::QObject;
    using Completion = std::function<void(QString path, QString error)>;
    virtual QString id() const = 0;
    virtual QString actionTitle() const = 0;
    // 只公布本提供者当前可调度的主机，创建时宿主再次校验。
    virtual QVariantMap readyHosts(const QString &source) const {
        return QFileInfo(source).isDir() ? QVariantMap{{"local", QStringLiteral("本机")}} : QVariantMap{};
    }
    virtual bool managesResources() const { return false; }
    using ResourceReady = std::function<void(QVariantMap)>;
    virtual void resourceInfo(const QString &, const QString &, bool, ResourceReady ready) { ready({}); }
    using FieldsReady = std::function<void(QVariantList)>;
    virtual void loadFields(const QString &, FieldsReady ready) { ready({}); }
    virtual void create(const QString &source, const QString &name, const QVariantMap &options, Completion done) = 0;
};
}
