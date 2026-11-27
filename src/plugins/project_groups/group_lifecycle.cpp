#include "group_lifecycle.h"
#include <QJsonArray>
namespace exagent::groups {
QJsonObject lifecycle(ProjectNavigator &nav, const QString &action,
                      const QJsonObject &p, ProjectCatalog *next,
                      QString *error) {
  const auto id = p["group"].toString();
  const auto *group = nav.catalog.find(id);
  if (!p["group"].isString() || !group || group->kind() != ProjectKind::Group) {
    *error = QStringLiteral("项目组不存在。");
    return {};
  }
  // 预览和提交都重验运行状态；确认框打开期间开始的任务也不能绕过保护。
  QStringList busy;
  QSet<QString> paths;
  for (const auto &project : nav.catalog.projects)
    if (project.id == id || project.groupId() == id)
      for (const auto &w : project.workspaces)
        paths.insert(w.path);
  if (!nav.runningTasks) {
    *error = QStringLiteral("运行保护尚未就绪。");
    return {};
  }
  for (const auto &path : paths)
    if (nav.organizingBusy(path) || !nav.runningTasks(path).isEmpty())
      busy.append(path);
  if (!busy.isEmpty()) {
    *error = QStringLiteral(
        "请先结束项目组内的任务、终端或工作树操作，再修改归属。");
    return {};
  }
  auto result = nav.catalog;
  if (action == "remove-member") {
    const auto member = p["member"].toString(),
               replacement = p["replacement"].toString();
    const auto *source = nav.catalog.find(member);
    if (!p["member"].isString() || !source || source->groupId() != id ||
        (p.contains("replacement") && !p["replacement"].isString())) {
      *error = QStringLiteral("待移除成员不属于此项目组。");
      return {};
    }
    int affected = 0;
    for (const auto &w : group->workspaces)
      if (w.launch.value("provider") == "folder.workspace" &&
          w.launch.value("taskSource") == member)
        ++affected;
    if (affected) {
      const auto *target = nav.catalog.find(replacement);
      if (!target || target->groupId() != id || replacement == member) {
        *error = QStringLiteral(
            "该成员仍是工作区的任务来源，请选择同组其他仓库替代。");
        return {};
      }
    } else if (!replacement.isEmpty()) {
      const auto *target = nav.catalog.find(replacement);
      if (!target || target->groupId() != id || replacement == member) {
        *error = QStringLiteral("替代仓库无效。");
        return {};
      }
    }
    for (auto &project : result.projects) {
      if (project.id == member)
        project.organization.clear();
      if (project.id == id)
        for (auto &w : project.workspaces)
          if (w.launch.value("provider") == "folder.workspace" &&
              w.launch.value("taskSource") == member)
            w.launch.insert("taskSource", replacement);
    }
    *next = std::move(result);
    return {{"group", id},
            {"member", member},
            {"replacement", replacement},
            {"affected", affected}};
  }
  if (action == "delete") {
    QJsonArray members;
    bool hasSessions = false;
    for (const auto &v : nav.rows()) {
      const auto r = v.toMap();
      if (r.value("project") == id &&
          r.value("kind").toInt() == ProjectNavigator::SessionRow)
        hasSessions = true;
    }
    const bool retained = hasSessions || !group->workspaces.isEmpty();
    for (auto &project : result.projects) {
      if (project.groupId() == id) {
        members.append(project.id);
        project.organization.clear();
      }
      if (project.id != id)
        continue;
      project.organization.clear();
      project.name += QStringLiteral("（保留会话）");
      for (auto &w : project.workspaces) {
        if (w.launch.value("provider") != "folder.workspace")
          continue;
        const auto *source =
            nav.catalog.find(w.launch.value("taskSource").toString());
        if (!source) {
          *error = QStringLiteral("任务来源失效，未修改项目组。");
          return {};
        }
        // 删除分组后冻结原Git根，普通项目不再依赖已解除的成员关系。
        w.launch.insert("provider", "folder.detached");
        w.launch.insert("repositoryRoot", source->rootPath());
        w.launch.remove("taskSource");
      }
    }
    if (!retained)
      for (qsizetype i = 0; i < result.projects.size(); ++i)
        if (result.projects[i].id == id) {
          result.projects.removeAt(i);
          break;
        }
    *next = std::move(result);
    return {{"group", id},
            {"members", members},
            {"retainedProject", retained ? QJsonValue(id) : QJsonValue()}};
  }
  *error = QStringLiteral("未知项目组生命周期操作。");
  return {};
}
} // namespace exagent::groups
