#pragma once
#include <QObject>
#include <QSettings>
#include <QVariantMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 终端配置独立于聊天和 Agent 身份，设置只影响随后创建的 Shell。
class TerminalPreferences final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
public:
    explicit TerminalPreferences(QObject *parent=nullptr) : QObject(parent) {}
    QVariantMap values() const;
    Q_INVOKABLE QString save(const QString &shell, const QString &arguments, const QString &commands);
    Q_INVOKABLE QString importTheme(const QUrl &url, int format);
    Q_INVOKABLE void resetTheme();
    static QStringList shellCommand();
signals:
    void changed();
private:
    QSettings m_settings;
};
}
