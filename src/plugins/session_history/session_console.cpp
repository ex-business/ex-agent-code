#include "terminal_scrollback.h"
#include <QRegularExpression>
#include "session_console.h"
#include "plugins/terminal_settings/terminal_preferences.h"
#include "plugins/agent_activity/activity_store.h"
#include "history_provider.h"
#include "terminal_backend.h"
#include "agent/cli/session_binding.h"
#include "agent/codex/thread_input.h"
#include "terminal_hibernation.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
namespace exagent {
namespace {
    QString snapshotPath(const QString& id)
    {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/terminal-snapshots";
        if (!id.startsWith("ex-") || QUuid(id.mid(3)).isNull() || !QDir().mkpath(dir))
            return {};
        return dir + "/" + id + ".txt";
    }
}
SessionConsole::SessionConsole(QObject* parent)
    : QObject(parent)
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    m_writer = std::make_unique<QLockFile>(directory + "/terminal-state.lock");
    m_writer->setStaleLockTime(0);
    if (!m_writer->tryLock())
        m_error = QStringLiteral("另一窗口正在管理终端，当前仅可查看。");
    QSettings s;
    m_sleepEnabled = s.value("terminal/hibernation", false).toBool();
    m_minutes = qBound(1, s.value("terminal/idleMinutes", 30).toInt(), 1440);
    auto* maintenance = new QTimer(this);
    maintenance->setInterval(5000);
    connect(maintenance, &QTimer::timeout, this, &SessionConsole::updateHibernation);
    connect(maintenance, &QTimer::timeout, this, &SessionConsole::refreshTerminalActivity);
    connect(this, &SessionConsole::tabsChanged, this, &SessionConsole::refreshTerminalActivity);
    QTimer::singleShot(0, this, &SessionConsole::refreshTerminalActivity);
    connect(maintenance, &QTimer::timeout, this, &SessionConsole::refreshConversationTabs);
    QTimer::singleShot(0,this,&SessionConsole::refreshConversationTabs);
    maintenance->start();
    m_tabs = s.value("terminal/tabs").toList();
    m_selected = qBound(-1, s.value("terminal/selected", -1).toInt(), int(m_tabs.size()) - 1);
    // 磁盘状态只证明画面可恢复；每次打开都重新向后端核对存活性。
    for (auto& t : m_tabs) {
        auto row = t.toMap();
        QFile snapshot(snapshotPath(row.value("id").toString()));
        if (snapshot.open(QIODevice::ReadOnly))
            row.insert("text", QString::fromUtf8(snapshot.read(2 * 1024 * 1024)));
        row.insert("state", hibernation::requested(row) ? Sleeping : Unknown);
        t = row;
    }
}
SessionConsole::~SessionConsole()
{
    if (m_writer->isLocked())
        for (const auto& tab : m_tabs)
            hibernation::revoke(tab.toMap().value("id").toString());
}
void SessionConsole::fail(const QString& message)
{
    m_error = message;
    emit changed();
}
void SessionConsole::save()
{
    if (!m_writer->isLocked())
        return;
    QVariantList metadata;
    for (const auto& t : m_tabs) {
        auto row = t.toMap();
        row.remove("text");
        metadata.append(row);
    }
    QSettings settings;
    settings.setValue("terminal/tabs", metadata);
    settings.setValue("terminal/selected", m_selected);
}
QVariantList SessionConsole::tabHeaders() const
{
    // 滚动内容变化无需重新创建页签，也无需将长回滚文本交给页签视图。
    QVariantList headers;
    for (const auto& tab : m_tabs) {
        const auto row = tab.toMap();
        headers.append(QVariantMap{{"id",row["id"]},{"title",row["title"]},{"cwd",row["cwd"]}});
    }
    return headers;
}
int SessionConsole::requestScrollback(const QString& id)
{
    const int serial = ++m_scrollbackSerial;
    QString fallback;
    bool known = false;
    for (const auto& tab : m_tabs) {
        const auto row = tab.toMap();
        if (row["id"] == id) {
            known = true;
            fallback = row["text"].toString();
            break;
        }
    }
    // 查找只读取指定页签的快照；保持运行程序的输入模式和屏幕位置。
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,serial] {
        emit scrollbackReady(serial,watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([known,id,fallback] {
        return known ? terminalSnapshot(id).value("text",fallback).toString() : QString();
    }));
    return serial;
}
int SessionConsole::requestHistory(const QString& id)
{
    const int serial=++m_scrollbackSerial;
    QString fallback;bool known=false;
    for(const auto &tab:m_tabs){const auto row=tab.toMap();if(row["id"]==id){known=true;fallback=row["text"].toString();break;}}
    auto *watcher=new QFutureWatcher<QVariantMap>(this);
    connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher,serial]{emit historyReady(serial,watcher->result());watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([known,id,fallback]{return known ? terminalScrollback(id,fallback) : QVariantMap{{"text",QString()},{"error",QStringLiteral("终端页签已不存在。")},{"limited",true}};}));
    return serial;
}
void SessionConsole::refreshActivity(const QString& cwd)
{
    const auto generation = ++m_activityGeneration;
    auto* watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher,&QFutureWatcher<QVariantList>::finished,this,[this,watcher,generation,cwd] {
        if (generation == m_activityGeneration) emit activityReady(cwd,watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(activity::recent,cwd));
}
QVariantMap SessionConsole::readMemory(const QString& cwd,const QString& path) const { return activity::memory(cwd,path); }
void SessionConsole::scan(const QString& workspace)
{
    const auto generation = ++m_generation;
    m_history.clear();
    emit changed();
    auto* w = new QFutureWatcher<QVariantList>(this);
    connect(w, &QFutureWatcher<QVariantList>::finished, this, [this, w, generation] {
        if (generation == m_generation) {
            m_history = w->result();
            emit changed();
        }
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(discoverSessions, workspace));
}
void SessionConsole::select(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;
    m_selected = index;
    updateHibernation();
    save();
    emit changed();
    poll();
}
QString SessionConsole::agentExecutable(const QString& command) const
{
    const QFileInfo info(command.contains('/') ? command : cliPath(command));
    if (command.isEmpty() || (command.contains('/') && !QDir::isAbsolutePath(command)))
        return {};
    return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
}
QString SessionConsole::launchAgent(const QString& title, const QString& cwd, const QString& command,
    const QString& arguments, const QString& environment)
{
    const QString path = agentExecutable(command);
    if (path.isEmpty()) return QStringLiteral("找不到可执行文件，请检查路径和执行权限。");
    const auto variables = environment.split('\n', Qt::SkipEmptyParts);
    static const QRegularExpression assignment("^[A-Za-z_][A-Za-z0-9_]*=");
    for (const auto& variable : variables)
        if (!assignment.match(variable).hasMatch()) return QStringLiteral("环境变量需要使用 NAME=value 格式。");
    // tmux 对单个命令字符串使用 shell；固定使用 env 加绝对路径以保持参数边界。
    QStringList launch { "/usr/bin/env", path };
    launch.append(arguments.split('\n', Qt::SkipEmptyParts));
    const auto previous = m_tabs.size();
    start(title, cwd.isEmpty() ? QDir::homePath() : cwd, launch, {}, variables);
    return previous == m_tabs.size() ? m_error : QString();
}
void SessionConsole::shell(const QString& cwd) { start("Shell", cwd.isEmpty() ? QDir::homePath() : cwd, TerminalPreferences::shellCommand(), {}); }
void SessionConsole::resume(int index)
{
    const auto row = m_history.value(index).toMap();
    if (row.isEmpty() || row["program"].toString().isEmpty() || !QFileInfo::exists(row["file"].toString())) {
        fail(QStringLiteral("CLI 或历史文件不可用。"));
        return;
    }
    const auto origin = row["file"].toString();
    for (int i = 0; i < m_tabs.size(); ++i) {
        const auto tab = m_tabs[i].toMap();
        if (tab["origin"] == origin
            && (hibernation::requested(tab) || terminalSnapshot(tab["id"].toString())["state"].toInt() == 1)) {
            select(i);
            return;
        }
    }
    start(row["agent"].toString(), row["cwd"].toString(),
        { row["program"].toString(), row["flag"].toString(), row["argument"].toString() }, origin);
}
void SessionConsole::start(const QString& title, const QString& cwd, const QStringList& command, const QString& origin, const QStringList& environment, const QString& conversation)
{
    if (!m_writer->isLocked()) {
        fail(QStringLiteral("终端已由另一窗口管理。"));
        return;
    }
    if (!QFileInfo(cwd).isDir()) {
        fail(QStringLiteral("原工作目录不存在。"));
        return;
    }
    const auto id = "ex-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    // 命令和参数独立传给 tmux，不将历史路径拼进 shell 脚本。
    QStringList args { "new-session", "-d", "-s", id, "-x", "100", "-y", "30", "-c", cwd };
    for (const auto& entry : environment) args << "-e" << entry;
    QStringList launch = command;
    bool supported = false;
    if (title == "Pi" && !origin.isEmpty() && !launch.isEmpty()) {
        const auto extension = hibernation::installExtension();
        if (!extension.isEmpty()) {
            args << "-e" << "EX_ACTIVITY_DIR=" + activity::directory(cwd) << "-e"
                 << "EX_SLEEP_DIR=" + hibernation::directory(id) << "-e"
                 << "EX_SLEEP_ORIGIN=" + origin;
            if(launch.contains("--continue-agent-terminal"))args << "-e" << "EX_PI_TERMINAL_EXTENSION="+extension;
            else {launch.insert(1, "--extension");launch.insert(2, extension);}
            supported = true;
        }
    }
    // tmux在创建窗格时固定回滚容量；先设置本应用独立服务的新窗格默认值。
    const auto r = terminalCommand(QStringList{"set-option","-g","history-limit","20000",";"} + args + launch);
    if (!r.ok) {
        fail(QString::fromUtf8(r.output));
        return;
    }
    terminalCommand({ "set-option", "-w", "-t", "=" + id + ":0", "remain-on-exit", "on" });
    m_error.clear();
    m_tabs.append(QVariantMap { { "id", id }, { "title", title }, { "cwd", cwd }, { "origin", origin },
        { "conversationId", conversation }, { "hibernatable", supported }, { "state", -1 } });
    emit tabsChanged();
    select(m_tabs.size() - 1);
}
void SessionConsole::poll(bool withText)
{
    if (m_polling || m_selected < 0)
        return;
    const auto index = m_selected;
    const auto id = current()["id"].toString();
    m_polling = true;
    auto* w = new QFutureWatcher<QVariantMap>(this);
    connect(w, &QFutureWatcher<QVariantMap>::finished, this, [this, w, index] {
        auto row = m_tabs[index].toMap();
        auto snapshot = w->result();
        if (hibernation::requested(row) && snapshot.value("state").toInt() == Ended) {
            snapshot.insert("state", Sleeping);
            snapshot.insert("text", hibernation::frame(row["id"].toString()));
        }
        w->deleteLater();
        m_polling = false;
        for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
            row.insert(it.key(), it.value());
        if (row != m_tabs[index].toMap()) {
            if (m_writer->isLocked() && snapshot.contains("text")
                && snapshot.value("text") != m_tabs[index].toMap().value("text")) {
                QSaveFile file(snapshotPath(row.value("id").toString()));
                const auto bytes = row.value("text").toString().toUtf8();
                if (file.open(QIODevice::WriteOnly)) {
                    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                    if (file.write(bytes) == bytes.size())
                        file.commit();
                }
            }
            m_tabs[index] = row;
            save();
            emit changed();
        }
        if (index != m_selected)
            poll();
    });
    w->setFuture(QtConcurrent::run(terminalSnapshot, id, withText));
}
void SessionConsole::sendNavigationKey(int key)
{
    if (!m_writer->isLocked() || m_selected < 0)
        return;
    const QHash<int, QString> keys { { Qt::Key_Up, "Up" }, { Qt::Key_Down, "Down" }, { Qt::Key_Escape, "Escape" },
        { Qt::Key_Return, "Enter" } };
    if (current()["state"].toInt() == Sleeping || !keys.contains(key))
        return;
    // CLI 的选择菜单需要独立按键，不能作为提示词写入会话。
    auto result = terminalCommand({ "send-keys", "-t", "=" + current()["id"].toString() + ":0.0", keys.value(key) });
    if (!result.ok)
        fail(QStringLiteral("终端按键发送失败：") + QString::fromUtf8(result.output));
    poll();
}

void SessionConsole::send(const QString& text)
{
    if (!m_writer->isLocked()) {
        fail(QStringLiteral("另一个窗口持有终端输入权。"));
        return;
    }
    if (current()["state"].toInt() == Sleeping || text.contains(QChar::Null) || text.size() > 8192 || m_selected < 0)
        return;
    const auto target = "=" + current()["id"].toString() + ":0.0";
    auto r = terminalCommand({ "send-keys", "-t", target, "-l", "--", text });
    if (r.ok)
        r = terminalCommand({ "send-keys", "-t", target, "Enter" });
    if (!r.ok)
        fail(QStringLiteral("发送失败：") + QString::fromUtf8(r.output));
    poll();
}
}

namespace exagent {
QStringList SessionConsole::sleepingWorkspaces() const
{
    QStringList result;
    for (const auto& t : m_tabs) {
        auto row = t.toMap();
        if (row["state"].toInt() == Sleeping)
            result.append(row["cwd"].toString());
    }
    result.removeDuplicates();
    return result;
}
void SessionConsole::configureHibernation(bool enabled, int minutes)
{
    if (!m_writer->isLocked())
        return;
    m_sleepEnabled = enabled;
    m_minutes = qBound(1, minutes, 1440);
    m_since.clear();
    QSettings s;
    s.setValue("terminal/hibernation", enabled);
    s.setValue("terminal/idleMinutes", m_minutes);
    updateHibernation();
    emit changed();
}
void SessionConsole::setDraft(const QString& id, const QString& text)
{
    if (id.isEmpty() || m_drafts.value(id) == text) return;
    // 草稿属于输入时的终端，切换标签后仍保护原终端。
    const bool wasPending = m_drafts.contains(id);
    if (text.isEmpty()) m_drafts.remove(id);
    else m_drafts.insert(id, text);
    if (wasPending == m_drafts.contains(id)) return;
    updateHibernation();
}
void SessionConsole::setConsoleVisible(bool visible)
{
    m_visible = visible;
    updateHibernation();
}
void SessionConsole::updateHibernation()
{
    if (!m_writer->isLocked())
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_tabs.size(); ++i) {
        auto row = m_tabs[i].toMap();
        if (!row["hibernatable"].toBool())
            continue;
        auto id = row["id"].toString();
        const bool foreground = m_visible && i == m_selected;
        if (hibernation::requested(row)) {
            hibernation::revoke(id);
            m_since.remove(id);
            if (row["state"].toInt() != Sleeping) {
                if (terminalSnapshot(id)["state"].toInt() == Running)
                    continue;
                row["state"] = Sleeping;
                row["text"] = hibernation::frame(id);
                m_tabs[i] = row;
                save();
                emit changed();
            }
            if (foreground)
                wake(i);
        } else if (!foreground && !m_drafts.contains(id) && m_sleepEnabled && row["state"].toInt() != Ended) {
            if (!m_since.contains(id))
                m_since.insert(id, now);
            hibernation::permit(id, m_minutes, m_since[id]);
        } else {
            m_since.remove(id);
            hibernation::revoke(id);
        }
    }
}
bool SessionConsole::wake(int index)
{
    auto row = m_tabs[index].toMap();
    auto id = row["id"].toString();
    const auto binding=row.value("conversationBinding").toMap();
    if(!binding.isEmpty() && !codex::threadAvailable(cli::leaseKey(QJsonObject::fromVariantMap(binding.value("source").toMap())))) {fail(QStringLiteral("原生聊天占用此会话，暂不唤醒终端。"));return false;}
    if (!hibernation::sameTranscript(row) || !QFileInfo(row["cwd"].toString()).isDir()) {
        fail(QStringLiteral("无法唤醒：原会话标识已变化，或原工作目录不可用。"));
        return false;
    }
    // 不强制替换任何存活进程；只让原窗格重新执行其既有参数。
    auto r = terminalCommand({ "respawn-pane", "-t", "=" + id + ":0.0", "-c", row["cwd"].toString() });
    if (!r.ok && !terminalCommand({ "has-session", "-t", "=" + id }).ok) {
        // tmux 服务也已退出时，只从已知 Pi 提供者与原会话文件重建，绝不执行持久化的任意命令。
        const auto program = cliPath("pi"), extension = hibernation::installExtension();
        if (row["title"] == "Pi" && !program.isEmpty() && !extension.isEmpty()) {
            const auto origin = row["origin"].toString();
            QStringList command{program,"--extension",extension,"--session",origin};
            QStringList env{"-e","EX_ACTIVITY_DIR="+activity::directory(row["cwd"].toString()),"-e","EX_SLEEP_DIR="+hibernation::directory(id),"-e","EX_SLEEP_ORIGIN="+origin};
            if(!binding.isEmpty()) {
                const auto configured=binding.value("native").toMap().value("program",program).toString();
                if(QFileInfo(configured).canonicalFilePath()!=QFileInfo(program).canonicalFilePath()){fail(QStringLiteral("请从聊天重新续接并核对自定义CLI。"));return false;}
                auto launch=binding;launch.insert("program",program);env<<"-e"<<"EX_PI_TERMINAL_EXTENSION="+extension;
                command={QCoreApplication::applicationFilePath(),"--continue-agent-terminal",QString::fromUtf8(QJsonDocument::fromVariant(launch).toJson(QJsonDocument::Compact).toBase64())};
            }
            r=terminalCommand(QStringList{"new-session","-d","-s",id,"-x","100","-y","30","-c",row["cwd"].toString()}+env+command);
            if (r.ok)
                terminalCommand({ "set-option", "-w", "-t", "=" + id + ":0", "remain-on-exit", "on" });
        }
    }
    if (!r.ok) {
        fail(QStringLiteral("唤醒失败：") + QString::fromUtf8(r.output));
        return false;
    }
    QFile::remove(hibernation::directory(id) + "/sleep");
    row["state"] = Unknown;
    m_notifiedHistory.remove(id);
    m_tabs[index] = row;
    save();
    emit changed();
    poll();
    return true;
}
}
