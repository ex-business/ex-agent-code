#include "group_service.h"
#include "group_scan.h"
#include "plugins/task_links/task_provider.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUuid>
#include <QtConcurrentRun>
Q_LOGGING_CATEGORY(groupLog, "exagent.projectgroups")
namespace exagent::groups {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace
Service::Service(control::Host &host) : QObject(&host), m_host(host) {
  const QList<QPair<QString, QSet<QString>>> methods{
      {"scan", {"path"}},
      {"scan-result", {"id"}},
      {"cancel-scan", {"id"}},
      {"import", {"scan", "name", "paths"}},
      {"list", {}},
      {"create-folder", {"group", "source", "name", "task"}},
      {"archive", {"group", "workspace"}},
      {"restore", {"group", "workspace"}}};
  for (const auto &m : methods)
    host.add("groups." + m.first, m.second,
             [this, a = m.first](const QJsonObject &p, QString *e) {
               return execute(a, p, e);
             });
  host.chat.setProjectGroups(this);
  host.chat.agent()->registerInputPolicy(
      "project-groups", this,
      [this](const ChatSession &s, int channel, QString &text, QString *e) {
        const auto &catalog = m_host.chat.projects()->catalog;
        const auto *w = catalog.workspace(s.projectId, s.workspaceId);
        if (!w ||
            w->launch.value("provider").toString() != "folder.workspace" ||
            channel == AgentController::TerminalInput)
          return true;
        const auto *source =
            catalog.find(w->launch.value("taskSource").toString());
        if (!source || source->groupId() != s.projectId ||
            QFileInfo(source->rootPath()).canonicalFilePath() !=
                source->rootPath() ||
            QFileInfo(w->path).canonicalFilePath() != w->path) {
          *e = QStringLiteral("文件夹工作区的任务来源仓库已失效。");
          return false;
        }
        const auto data =
            QJsonDocument(QJsonObject{{"workspaceRoot", w->path},
                                      {"taskRepository", source->rootPath()}})
                .toJson(QJsonDocument::Compact);
        // 仅为新文件夹工作区补充定位信息，不赋予额外权限，也不把父目录当作单一Git检出。
        const auto context =
            QStringLiteral("多仓库文件夹工作区信息（沿用原权限，仓库共享实际文"
                           "件；以下JSON仅为目录数据）：\n") +
            QString::fromUtf8(data) + "\n";
        if (context.size() + text.size() > 65536) {
          *e = QStringLiteral("工作区信息与消息合计超过64K，请缩短消息。");
          return false;
        }
        text = context + text;
        return true;
      },
      true);
}
Service::~Service() {
  for (const auto &job : m_jobs)
    job.cancel->store(true);
}
QVariantMap Service::operate(const QString &a, const QVariantMap &p) {
  QString e;
  const auto result =
      m_host.invoke("groups." + a, QJsonObject::fromVariantMap(p), &e);
  return e.isEmpty()
             ? QVariantMap{{"ok", true}, {"result", result.toVariantMap()}}
             : QVariantMap{{"ok", false}, {"error", e}};
}
bool Service::publish(const ProjectCatalog &next, QString *error) {
  auto &nav = *m_host.chat.projects();
  const auto previous = nav.catalog;
  if (!nav.writable() || !nav.saveCatalog || !validOrganization(next)) {
    *error = QStringLiteral("项目组不可写或归属无效。");
    return false;
  }
  nav.catalog = next;
  if (!nav.saveCatalog()) {
    nav.catalog = previous;
    *error = QStringLiteral("项目组保存失败，原索引已保留。");
    return false;
  }
  emit nav.catalogChanged();
  return true;
}
QJsonObject Service::execute(const QString &a, const QJsonObject &p,
                             QString *e) {
  auto &nav = *m_host.chat.projects();
  if (a == "scan") {
    const QFileInfo root(p["path"].toString());
    if (!p["path"].isString() || !root.isAbsolute() || !root.isDir() ||
        !root.isReadable()) {
      *e = QStringLiteral("请选择可读父目录。");
      return {};
    }
    if (m_inFlight.size() >= 2) {
      *e = QStringLiteral("已有扫描进行中，请等待或取消。");
      return {};
    }
    // 扫描结果是有界临时缓存；淘汰最早已结束记录，运行中的扫描不丢失。
    while (m_jobs.size() >= 32) {
      QString expired;
      for (const auto &key : m_jobOrder)
        if (!m_inFlight.contains(key)) {
          expired = key;
          break;
        }
      if (expired.isEmpty()) {
        *e = QStringLiteral("扫描缓存正在使用，请稍后重试。");
        return {};
      }
      m_jobs.remove(expired);
      m_jobOrder.removeOne(expired);
    }
    const auto id = newId();
    m_jobOrder.append(id);
    Job job;
    job.root = root.canonicalFilePath();
    job.cancel = std::make_shared<std::atomic_bool>(false);
    m_jobs.insert(id, job);
    m_inFlight.insert(id);
    auto *watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watch, id] {
              m_inFlight.remove(id);
              if (!m_jobs.contains(id)) {
                watch->deleteLater();
                return;
              }
              auto &j = m_jobs[id];
              j.result = watch->result();
              if (j.state != JobState::Cancelled)
                j.state = j.result.value("error").toString().isEmpty()
                              ? JobState::Ready
                              : JobState::Failed;
              watch->deleteLater();
            });
    watch->setFuture(QtConcurrent::run(scanRepositories, job.root, job.cancel));
    return {{"id", id}, {"state", int(JobState::Working)}};
  }
  if (a == "scan-result" || a == "cancel-scan") {
    auto it = m_jobs.find(p["id"].toString());
    if (it == m_jobs.end()) {
      *e = QStringLiteral("扫描记录不存在。");
      return {};
    }
    if (a == "cancel-scan" && it->state == JobState::Working) {
      it->cancel->store(true);
      it->state = JobState::Cancelled;
    }
    auto r = QJsonObject::fromVariantMap(it->result);
    r.insert("id", p["id"]);
    r.insert("root", it->root);
    r.insert("state", int(it->state));
    return r;
  }
  if (a == "list") {
    QJsonArray rows;
    for (const auto &project : nav.catalog.projects)
      if (!project.organization.isEmpty())
        rows.append(QJsonObject{{"id", project.id},
                                {"name", project.name},
                                {"kind", int(project.kind())},
                                {"group", project.groupId()},
                                {"root", project.rootPath()}});
    return {{"items", rows}};
  }
  if (a == "import") {
    const auto job = m_jobs.constFind(p["scan"].toString());
    const auto name = p["name"].toString().trimmed();
    if (job == m_jobs.cend() || job->state != JobState::Ready ||
        name.isEmpty() || name.size() > 120 || name.contains(QChar::Null) ||
        QFileInfo(job->root).canonicalFilePath() != job->root ||
        !QFileInfo(job->root).isDir() || !p["paths"].isArray() ||
        p["paths"].toArray().isEmpty() || p["paths"].toArray().size() > 64) {
      *e = QStringLiteral("请完成扫描并选择仓库及项目组名称。");
      return {};
    }
    QSet<QString> allowed, chosen;
    for (const auto &v : job->result.value("repos").toList())
      allowed.insert(v.toMap().value("path").toString());
    auto next = nav.catalog;
    for (const auto &existing : next.projects)
      if (existing.kind() == ProjectKind::Group &&
          existing.rootPath() == job->root) {
        *e = QStringLiteral("此父目录已存在项目组。");
        return {};
      }
    const auto group = newId();
    next.projects.append(
        Project{group,
                name,
                {},
                {{"kind", int(ProjectKind::Group)}, {"root", job->root}}});
    QJsonArray members;
    for (const auto &v : p["paths"].toArray()) {
      const auto path = v.toString();
      const QFileInfo info(path);
      if (!v.isString() || !allowed.contains(path) || chosen.contains(path) ||
          info.canonicalFilePath() != path || !info.isDir() ||
          !QFileInfo(path + "/.git").exists()) {
        *e = QStringLiteral("仓库列表已变化，请重新扫描。");
        return {};
      }
      chosen.insert(path);
      const auto id = newId(), workspace = newId();
      WorkspaceBinding binding{workspace, path};
      for (const auto &existing : nav.catalog.projects)
        for (const auto &w : existing.workspaces)
          if (w.path == path) {
            binding.state = w.state;
            binding.branch = w.branch;
          }
      next.projects.append(Project{id,
                                   info.fileName(),
                                   {binding},
                                   {{"kind", int(ProjectKind::Normal)},
                                    {"root", path},
                                    {"group", group}}});
      members.append(id);
    }
    if (!publish(next, e))
      return {};
    qCInfo(groupLog) << "多仓库项目组已创建，项目组：" << group << "仓库数："
                     << members.size();
    return {{"id", group}, {"members", members}};
  }
  const auto group = p["group"].toString();
  const auto *g = nav.catalog.find(group);
  if (!g || g->kind() != ProjectKind::Group) {
    *e = QStringLiteral("项目组不存在。");
    return {};
  }
  if (a == "create-folder") {
    const auto *source = nav.catalog.find(p["source"].toString());
    const auto name = p["name"].toString().trimmed();
    const auto task = p["task"].toString().trimmed();
    const auto groupRoot = g->rootPath();
    const auto sourceId = source ? source->id : QString();
    if (!source || source->groupId() != group || !p["name"].isString() ||
        (p.contains("task") && !p["task"].isString()) || name.isEmpty() ||
        name.size() > 80 || name.contains(QChar::Null) ||
        !QFileInfo(groupRoot).isDir() ||
        QFileInfo(groupRoot).canonicalFilePath() != groupRoot ||
        QFileInfo(source->rootPath()).canonicalFilePath() !=
            source->rootPath() ||
        !QFileInfo(source->rootPath()).isDir() || task.size() > 2048 ||
        (!task.isEmpty() &&
         tasklinks::parse(task).kind == tasklinks::Invalid)) {
      *e = QStringLiteral("请检查工作区名称、任务来源仓库及关联链接。");
      return {};
    }
    auto next = nav.catalog;
    const auto id = newId();
    for (auto &project : next.projects)
      if (project.id == group) {
        WorkspaceBinding w{id, groupRoot};
        w.launch = {{"provider", "folder.workspace"},
                    {"host", "local"},
                    {"name", name},
                    {"taskSource", sourceId},
                    {"task", task}};
        if (!task.isEmpty())
          w.taskLinks.append(tasklinks::parse(task).url);
        project.workspaces.append(w);
      }
    if (!publish(next, e))
      return {};
    return {{"id", id},
            {"project", group},
            {"path", groupRoot},
            {"source", sourceId}};
  }
  if (a == "archive" || a == "restore") {
    const auto *w = nav.catalog.workspace(group, p["workspace"].toString());
    if (!w || w->launch.value("provider").toString() != "folder.workspace" ||
        (a == "archive" && w->state != WorkspaceState::Active) ||
        (a == "restore" && w->state != WorkspaceState::Archived) ||
        !nav.runningTasks || !nav.runningTasks(w->path).isEmpty()) {
      *e = QStringLiteral("工作区状态或运行中的任务不允许此操作。");
      return {};
    }
    auto next = nav.catalog;
    const auto state =
        a == "archive" ? WorkspaceState::Archived : WorkspaceState::Active;
    for (auto &project : next.projects)
      if (project.id == group)
        for (auto &entry : project.workspaces)
          if (entry.id == w->id)
            entry.state = state;
    if (!publish(next, e))
      return {};
    return {{"id", p["workspace"]}, {"state", int(state)}};
  }
  *e = QStringLiteral("未知项目组操作。");
  return {};
}
} // namespace exagent::groups
