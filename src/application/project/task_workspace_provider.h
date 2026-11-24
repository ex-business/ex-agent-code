#pragma once
#include <QObject>
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
    using FieldsReady = std::function<void(QVariantList)>;
    virtual void loadFields(const QString &, FieldsReady ready) { ready({}); }
    virtual void create(const QString &source, const QString &name, const QVariantMap &options, Completion done) = 0;
};
}
