#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class NotificationCenter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int unread READ unread NOTIFY changed)
    Q_PROPERTY(bool attention READ attention WRITE setAttention NOTIFY settingsChanged)
    Q_PROPERTY(bool outcomes READ outcomes WRITE setOutcomes NOTIFY settingsChanged)
    Q_PROPERTY(bool desktop READ desktop WRITE setDesktop NOTIFY settingsChanged)
    Q_PROPERTY(bool badge READ badge WRITE setBadge NOTIFY settingsChanged)
    Q_PROPERTY(bool managesBadge READ managesBadge WRITE setManagesBadge)
    Q_PROPERTY(bool sound READ sound WRITE setSound NOTIFY settingsChanged)
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString desktopBody MEMBER m_desktopBody)
    Q_PROPERTY(QString deliveryError READ deliveryError NOTIFY deliveryErrorChanged)
public:
    enum Category { Attention = 1, Completed, Failed, Stopped, Interrupted };
    Q_ENUM(Category)
    explicit NotificationCenter(QObject *parent=nullptr);
    QVariantList entries() const { return m_entries; }
    int unread() const;
    bool attention() const { return m_attention; }
    bool outcomes() const { return m_outcomes; }
    bool desktop() const { return m_desktop; }
    bool badge() const { return m_badge; }
    bool managesBadge() const { return m_managesBadge; }
    void setBadge(bool value);
    void setManagesBadge(bool value);
    bool sound() const { return m_sound; }
    bool nativeAvailable() const;
    QString desktopBody() const { return m_desktopBody; }
    QString deliveryError() const { return m_error; }
    void setAttention(bool value);
    void setOutcomes(bool value);
    void setDesktop(bool value);
    void setSound(bool value);
    void setDeliveryError(const QString &error);
    Q_INVOKABLE void record(int category, const QString &session, const QString &run, const QString &token, const QString &title);
    Q_INVOKABLE void observeTerminals(const QVariantList &rows);
    Q_INVOKABLE void mark(const QString &id, bool read);
    Q_INVOKABLE bool matchesRequest(const QVariantMap &entry, const QString &token) const;
signals:
    void changed();
    void settingsChanged();
    void deliveryErrorChanged();
    void activated(QString id);
private:
    void append(int category, QString key, QVariantMap row);
    void save();
    void updateBadge();
    QVariantList m_entries;
    QHash<QString,int> m_terminalStates;
    QString m_error, m_desktopBody;
    bool m_attention, m_outcomes, m_desktop, m_sound, m_badge, m_managesBadge = false;
};
}
