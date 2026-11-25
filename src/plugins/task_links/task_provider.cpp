#include "task_provider.h"
#include <QJsonDocument>
#include <QRegularExpression>

namespace exagent::tasklinks {
namespace {
using Match = QRegularExpressionMatch;
using Parser = Link (*)(const QString &, const Match &);
struct Provider { QRegularExpression pattern; Parser parse; };

Link github(const QString &, const Match &m)
{
    const auto repo = m.captured(1).toLower(), number = m.captured(3);
    if (repo.section('/',1) == "." || repo.section('/',1) == "..") return {};
    const bool pr = m.captured(2) == "pull";
    return {pr ? PullRequest : GithubIssue, "https://github.com/" + repo + "/" + m.captured(2) + "/" + number,
            (pr ? "PR · " : "Issue · ") + repo + "#" + number, number};
}
Link linear(const QString &, const Match &m)
{
    const auto team = m.captured(1).toLower(), key = m.captured(2).toUpper();
    return {LinearIssue, "https://linear.app/" + team + "/issue/" + key, "Linear · " + team + "/" + key, key};
}
Link jira(const QString &host, const Match &m)
{
    const auto key = m.captured(1).toUpper();
    return {JiraIssue, "https://" + host + "/browse/" + key, "Jira · " + host.section('.',0,0) + "/" + key, key};
}
}
Link parse(const QString &input)
{
    if (input.size() > 2048) return {};
    const QUrl url(input.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.authority() != url.host()) return {};
    static const Provider providers[] = {
        {QRegularExpression(R"(^github\.com/([\w-]+/[\w.-]+)/(pull|issues)/([1-9][0-9]{0,11})(?:/(?:files|commits|checks))?/?$)"), github},
        {QRegularExpression(R"(^linear\.app/([a-zA-Z0-9_-]+)/issue/([A-Za-z][A-Za-z0-9_]*-[1-9][0-9]*)(?:/[^/]*)?/?$)"), linear},
        {QRegularExpression(R"(^[a-z0-9][a-z0-9-]*\.atlassian\.net/browse/([A-Za-z][A-Za-z0-9_]*-[1-9][0-9]*)/?$)"), jira}
    };
    const auto target = url.host() + url.path(QUrl::FullyEncoded);
    for (const auto &provider : providers) {
        const auto match = provider.pattern.match(target);
        if (match.hasMatch()) return provider.parse(url.host(),match);
    }
    return {};
}

Lookup lookup(const Link &link)
{
    Lookup result;
    auto &req = result.request;
    const QUrl url(link.url);
    // 手动重定向防止凭据跨域。仅在用户刷新状态时读取环境，不将凭据写入项目。
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    req.setTransferTimeout(12000);
    req.setRawHeader("Accept","application/json");
    if (link.kind == PullRequest || link.kind == GithubIssue) {
        req.setUrl(QUrl("https://api.github.com/repos/" + url.path().section('/',1,2)
                        + (link.kind == PullRequest ? "/pulls/" : "/issues/") + link.key));
        const auto token = qgetenv("GH_TOKEN");
        if (!token.isEmpty()) req.setRawHeader("Authorization","Bearer " + token);
    } else if (link.kind == LinearIssue) {
        const auto token = qgetenv("LINEAR_API_KEY");
        if (token.isEmpty()) { result.error = QStringLiteral("请配置 LINEAR_API_KEY。"); return result; }
        req.setUrl(QUrl("https://api.linear.app/graphql"));
        req.setRawHeader("Authorization",token);
        req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        result.body = QJsonDocument(QJsonObject{{"query","query LinkedIssue($id:String!){issue(id:$id){identifier url title state{name}}}"},
            {"variables",QJsonObject{{"id",link.key}}}}).toJson(QJsonDocument::Compact);
    } else if (link.kind == JiraIssue) {
        req.setUrl(QUrl("https://" + url.host() + "/rest/api/3/issue/" + link.key + "?fields=summary,status"));
        const auto site = QUrl(qEnvironmentVariable("EX_JIRA_SITE"));
        const auto email = qgetenv("JIRA_EMAIL"), token = qgetenv("JIRA_API_TOKEN");
        if (site == QUrl("https://" + url.host()) || site == QUrl("https://" + url.host() + "/")) {
            if (!email.isEmpty() && !token.isEmpty())
                req.setRawHeader("Authorization","Basic " + (email + ':' + token).toBase64());
        }
    }
    return result;
}

Snapshot decode(const Link &link, const QJsonObject &response)
{
    Snapshot result;
    if (link.kind == GithubIssue || link.kind == PullRequest) {
        if (parse(response["html_url"].toString()).url != link.url) return {};
        result.title = response["title"].toString();
        result.status = response["merged"].toBool() ? "merged" : response["draft"].toBool() && response["state"] == "open" ? "draft" : response["state"].toString();
    } else if (link.kind == LinearIssue) {
        const auto issue = response["data"].toObject()["issue"].toObject();
        // 相同工单编号可出现在不同组织；拒绝将另一组织的结果发布到此关联。
        if (response.contains("errors") || parse(issue["url"].toString()).url != link.url) return {};
        result.title = issue["title"].toString();
        result.status = issue["state"].toObject()["name"].toString();
    } else if (link.kind == JiraIssue && response["key"].toString() == link.key) {
        const auto fields = response["fields"].toObject();
        result.title = fields["summary"].toString();
        result.status = fields["status"].toObject()["name"].toString();
    }
    return {result.title.left(512),result.status.left(128)};
}
}
