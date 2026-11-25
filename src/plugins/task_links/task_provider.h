#pragma once
#include <QJsonObject>
#include <QNetworkRequest>

namespace exagent::tasklinks {
enum Kind { Invalid = 0, PullRequest = 1, GithubIssue = 2, LinearIssue = 3, JiraIssue = 4 };

struct Link {
    Kind kind = Invalid;
    QString url, label, key;
};
struct Lookup {
    QNetworkRequest request;
    QByteArray body;
    QString error;
};
struct Snapshot {
    QString title, status;
};
// 只有提供者识别的规范地址可持久化、打开或用于构造 API 请求。
Link parse(const QString &input);
Lookup lookup(const Link &link);
Snapshot decode(const Link &link, const QJsonObject &response);
}
