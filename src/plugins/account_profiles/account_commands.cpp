#include "account_commands.h"
#include "account_profiles.h"
#include "plugins/workbench_control/control_host.h"
#include <QJsonArray>
namespace exagent::accounts {
void registerCommands(control::Host &host) {
  auto &chat = host.chat;
  host.add("accounts.list", {}, [&chat](const QJsonObject &, QString *) {
    return QJsonObject{
        {"items",
         QJsonArray::fromVariantList(
             chat.agent()->accountProfiles()->property("rows").toList())}};
  });
  host.add(
      "accounts.create", {"kind", "name", "credentialFile", "model", "scope"},
      [&chat](const QJsonObject &p, QString *error) {
        auto *profiles =
            qobject_cast<accounts::Profiles *>(chat.agent()->accountProfiles());
        const auto id =
            profiles->create(p["kind"].toInt(), p["name"].toString(),
                             p["credentialFile"].toString(),
                             p["model"].toString(), p["scope"].toString());
        if (id.isEmpty())
          *error = profiles->error();
        return QJsonObject{{"id", id}};
      });
  host.add(
      "accounts.select", {"id", "model", "automatic"},
      [&chat](const QJsonObject &p, QString *error) {
        if (!p["automatic"].isBool() ||
            !chat.agent()->useProfile(p["id"].toString(), p["model"].toString(),
                                      p["automatic"].toBool()))
          *error = QStringLiteral("账号切换失败，请核对模型、权限和账号状态。");
        return QJsonObject{{"selected", chat.agent()->selectedProfile()},
                           {"running", chat.agent()->runningProfile()}};
      });
}
} // namespace exagent::accounts
