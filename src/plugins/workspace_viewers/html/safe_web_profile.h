#pragma once
#include <QQuickWebEngineProfile>
#include <QtQml/qqmlregistration.h>
namespace exagent {
void registerWorkspaceWebScheme();
class SafeWebProfile : public QQuickWebEngineProfile {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl navigationUrl READ navigationUrl NOTIFY changed)
public:
    explicit SafeWebProfile(QObject *parent=nullptr);
    QUrl navigationUrl() const { return m_url; }
    Q_INVOKABLE void configure(const QString &root,const QString &path,const QString &source,bool mermaid,bool dark=false);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool permits(const QUrl &url) const;
signals: void changed();
private:
    QUrl m_url;
    class Handler; Handler *m_handler;
};
}
