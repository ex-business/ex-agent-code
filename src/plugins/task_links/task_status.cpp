#include "task_status.h"
#include <QDesktopServices>
#include <QJsonDocument>
#include <QTimer>

namespace exagent {
QVariantList TaskStatus::taskRows(const QVariantList &workspaces) const
{
    QVariantList rows;
    for (const auto &workspace : workspaces) {
        const auto links = workspace.toMap().value("taskLinks").toStringList();
        for (const auto &url : links) {
            const auto link = describe(url);
            if (!link.isEmpty()) rows.append(QVariantMap{{"ws", workspace}, {"link", link}});
        }
    }
    return rows;
}

QVariantMap TaskStatus::describe(const QString &url) const
{
    const auto link = tasklinks::parse(url);
    if (link.kind == tasklinks::Invalid) return {};
    return {{"url",link.url},{"label",link.label},{"kind",int(link.kind)}};
}
bool TaskStatus::openSource(const QString &url) const
{
    const auto link = tasklinks::parse(url);
    if (link.kind == tasklinks::Invalid) return false;
    return QDesktopServices::openUrl(QUrl(link.url + (link.kind == tasklinks::PullRequest ? "/files" : "")));
}
void TaskStatus::reset()
{
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        reply->abort();
    }
    m_snapshot = {};
    m_error.clear();
    emit changed();
}
void TaskStatus::refresh(const QString &url)
{
    reset();
    const auto link = tasklinks::parse(url);
    if (link.kind == tasklinks::Invalid) return;
    const auto lookup = tasklinks::lookup(link);
    if (!lookup.error.isEmpty()) {
        m_error = lookup.error;
        emit changed();
        return;
    }
    auto *reply = lookup.body.isEmpty() ? m_network.get(lookup.request) : m_network.post(lookup.request,lookup.body);
    m_reply = reply;
    constexpr int limit = 2 * 1024 * 1024;
    reply->setReadBufferSize(limit + 1);
    connect(reply,&QIODevice::readyRead,this,[reply] {
        if (reply->bytesAvailable() > limit) reply->abort();
    });
    QTimer::singleShot(15000,reply,[reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply,link] {
        reply->deleteLater();
        // 切换任务、关闭或重新刷新后，仅当前请求可以更新面板。
        if (m_reply != reply) return;
        m_reply.clear();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http == 200 && reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= limit)
            m_snapshot = tasklinks::decode(link,QJsonDocument::fromJson(reply->readAll()).object());
        if (m_snapshot.status.isEmpty()) {
            if (http == 401 || http == 403) m_error = QStringLiteral("访问被拒绝，请检查任务读取权限。");
            else if (http == 404) m_error = QStringLiteral("未找到任务或没有读取权限。");
            else m_error = QStringLiteral("无法读取任务状态。");
        }
        emit changed();
    });
    emit changed();
}
}
