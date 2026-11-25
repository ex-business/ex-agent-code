#include "browser_catalog.h"
#include <QCryptographicHash>
#include <QSettings>
#include <QUuid>
#include <QRegularExpression>

namespace exagent {
namespace {
QString scope(const QString &workspace)
{
    return "workspaceBrowser/" + QString::fromLatin1(QCryptographicHash::hash(workspace.toUtf8(), QCryptographicHash::Sha256).toHex()) + '/';
}
}
QVariantList BrowserCatalog::profiles(const QString &workspace) const
{
    QVariantList rows;
    if (workspace.isEmpty()) return rows;
    rows.append(QVariantMap{{"id","default"},{"name",QStringLiteral("默认配置")}});
    for (const auto &entry : QSettings().value(scope(workspace)+"profiles").toList()) {
        const auto row=entry.toMap();
        if (!QUuid(row.value("id").toString()).isNull() && !row.value("name").toString().isEmpty())
            rows.append(row);
    }
    return rows;
}
QString BrowserCatalog::selected(const QString &workspace) const
{
    const QString id=QSettings().value(scope(workspace)+"selected","default").toString();
    for (const auto &entry : profiles(workspace)) if (entry.toMap()["id"]==id) return id;
    return workspace.isEmpty() ? QString{} : QStringLiteral("default");
}
QString BrowserCatalog::create(const QString &workspace, const QString &name)
{
    const QString label=name.trimmed();
    if (workspace.isEmpty() || label.isEmpty() || label.size()>64) return {};
    const auto rows=profiles(workspace);
    for (const auto &entry : rows)
        if (entry.toMap()["name"].toString().compare(label,Qt::CaseInsensitive)==0) return {};
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto custom=rows.mid(1);
    custom.append(QVariantMap{{"id",id},{"name",label}});
    QSettings().setValue(scope(workspace)+"profiles",custom);
    return id;
}
bool BrowserCatalog::select(const QString &workspace, const QString &profile)
{
    if (partition(workspace,profile).isEmpty()) return false;
    QSettings().setValue(scope(workspace)+"selected",profile);
    return true;
}
QString BrowserCatalog::partition(const QString &workspace, const QString &profile) const
{
    for (const auto &entry : profiles(workspace)) if (entry.toMap()["id"]==profile) {
        const QByteArray identity=scope(workspace).toUtf8()+profile.toUtf8();
        return "ex-browser-"+QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
    }
    return {};
}
bool BrowserCatalog::allowed(const QUrl &url) const
{
    return url.isValid() && (url.scheme()=="http" || url.scheme()=="https") && !url.host().isEmpty() && url.userInfo().isEmpty();
}
QUrl BrowserCatalog::address(const QString &input) const
{
    const QString text=input.trimmed();
    if (text.isEmpty() || text.contains(QRegularExpression("\\s")) || text.startsWith('/') || text.contains('\\')) return {};
    // 本地开发地址的端口不是协议；未提供协议的普通域名默认走 HTTPS。
    const bool hasScheme=text.contains("://");
    QUrl url(hasScheme ? text : "https://"+text, QUrl::StrictMode);
    const auto host=url.host();
    if (!hasScheme && (host=="localhost" || host=="::1" || QRegularExpression("^127(\\.[0-9]{1,3}){3}$").match(host).hasMatch()))
        url.setScheme("http");
    return allowed(url) ? url : QUrl{};
}
QVariantMap BrowserCatalog::restore(const QString &partition) const
{
    return QSettings().value("browserTabs/"+partition).toMap();
}
void BrowserCatalog::save(const QString &partition, const QStringList &urls, int active)
{
    if (partition.isEmpty()) return;
    // 整组写入，页签顺序与活动索引保持一致。页面标题不作为可执行地址。
    QSettings().setValue("browserTabs/"+partition,QVariantMap{{"urls",urls},{"active",active}});
}
}
