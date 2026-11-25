#include "activity_store.h"
#include "plugins/session_history/session_console.h"
#include "plugins/session_history/terminal_backend.h"
#include "domain/content/content_block.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QtConcurrentRun>

namespace exagent {
namespace {
QVariantList readPiActivity(const QVariantList &tabs, bool ownsInput)
{
    // 一次核对所有窗格的真实进程，只读取已绑定页签的定长状态快照。
    const auto panes = terminalCommand({"list-panes", "-a", "-f", "#{&&:#{==:#{window_index},0},#{==:#{pane_index},0}}",
        "-F", "#{session_name} #{pane_pid} #{pane_dead}"});
    QHash<QString, qint64> live;
    if (panes.ok) for (const auto &line : panes.output.split('\n')) {
        const auto cols = line.split(' ');
        if (cols.size() == 3 && cols[2] == "0") live.insert(QString::fromUtf8(cols[0]), cols[1].toLongLong());
    }
    QVariantList rows;
    using State = content::TaskState;
    for (const auto &entry : tabs) {
        const auto tab = entry.toMap();
        const auto id = tab["id"].toString();
        if (!live.contains(id)) continue;
        const auto cwd = tab["cwd"].toString();
        const auto dir = activity::directory(cwd);
        if (dir.isEmpty()) continue;
        const auto hash = QCryptographicHash::hash(tab["origin"].toString().toUtf8(), QCryptographicHash::Sha256).toHex();
        QFile file(dir + "/pi-" + QString::fromLatin1(hash) + ".json");
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8192) continue;
        const auto snapshot = QJsonDocument::fromJson(file.read(8192)).toVariant().toMap();
        // 旧快照可能属于另一个窗口或已退出的 Pi，不能把它重新变为可操作请求。
        if (snapshot.value("terminalId") != id || snapshot.value("pid").toLongLong() != live[id]) continue;
        const int status = snapshot.value("status", -1).toInt();
        const bool waiting = status == activity::Waiting && ownsInput;
        const bool working = status == activity::Working || status == activity::Waiting;
        const int state = working ? int(State::Running) : status == activity::Done ? int(State::Complete)
            : status == activity::Failed ? int(State::Failed) : status == activity::Stopped ? int(State::Cancelled) : int(State::Created);
        rows.append(QVariantMap{{"terminal", id}, {"title", QStringLiteral("Pi · ") + tab["origin"].toString().section('/', -1)},
            {"workspace", cwd}, {"time", snapshot["updated"]}, {"state", state}, {"idle", status == activity::Unknown},
            {"pending", waiting ? 1 : 0}, {"rank", waiting ? 0 : working ? 1 : 2}});
    }
    return rows;
}
}
void SessionConsole::refreshTerminalActivity()
{
    if (m_readingActivity) return;
    QVariantList tabs;
    for (const auto &entry : std::as_const(m_tabs)) {
        const auto tab = entry.toMap();
        if (tab.value("hibernatable").toBool())
            tabs.append(QVariantMap{{"id", tab["id"]}, {"origin", tab["origin"]}, {"cwd", tab["cwd"]}});
    }
    if (tabs.isEmpty()) return;
    m_readingActivity = true;
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        m_readingActivity = false;
        const auto next = watcher->result();
        if (next != m_terminalActivity) {
            m_terminalActivity = next;
            emit terminalActivityChanged();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(readPiActivity, tabs, ownsInput()));
}
} // namespace exagent
