#pragma once
#include <QObject>
#include <QFont>
#include <QVariantMap>
#include <QTimer>
#include <atomic>
#include <memory>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class PreparationQueue;
// 后台只生成可跨线程的值数据，TextEdit等界面对象始终留在GUI线程。
class RichTextPreparation : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(RichTextPreparation)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY inputChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY inputChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY inputChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY inputChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool deferUncached READ deferUncached WRITE setDeferUncached NOTIFY inputChanged)
    Q_PROPERTY(int preparationPriority READ preparationPriority WRITE setPreparationPriority NOTIFY inputChanged)
    Q_PROPERTY(qreal layoutWidth READ layoutWidth WRITE setLayoutWidth NOTIFY inputChanged)
    Q_PROPERTY(bool holdLayout READ holdLayout WRITE setHoldLayout NOTIFY inputChanged)
    Q_PROPERTY(QVariantMap layoutHint READ layoutHint NOTIFY inputChanged)
    Q_PROPERTY(QVariantMap value READ value NOTIFY valueChanged)
public:
    enum State { Idle = 0, Queued = 1, Running = 2, Ready = 3, Failed = 4 };
    Q_ENUM(State)
    enum Mode { Prose = 0, User = 1 };
    Q_ENUM(Mode)
    explicit RichTextPreparation(QObject *parent = nullptr);
    ~RichTextPreparation() override;
    QString source() const { return m_source; }
    QFont font() const { return m_font; }
    bool dark() const { return m_dark; }
    int mode() const { return m_mode; }
    int state() const { return m_state; }
    bool deferUncached() const { return m_deferUncached; }
    void setDeferUncached(bool defer);
    int preparationPriority() const { return m_priority; }
    void setPreparationPriority(int priority);
    qreal layoutWidth() const { return m_layoutWidth; }
    bool holdLayout() const { return m_holdLayout; }
    QVariantMap layoutHint() const;
    void setLayoutWidth(qreal width);
    void setHoldLayout(bool hold);
    QVariantMap value() const { return m_value; }
    void setSource(const QString &source);
    void setFont(const QFont &font);
    void setDark(bool dark);
    void setMode(int mode);
signals:
    void inputChanged();
    void stateChanged();
    void valueChanged();
    void layoutAboutToChange();
    void layoutChanged();
private:
    friend class PreparationQueue;
    void schedule();
    void submit();
    void setState(State state);
    void apply(quint64 generation, QVariantMap value, bool cached = false);
    QString m_source;
    QFont m_font;
    bool m_dark = false, m_waiting = false, m_deferUncached = false;
    int m_mode = Prose;
    int m_priority = 100;
    State m_state = Idle;
    quint64 m_generation = 0;
    QTimer m_schedule;
    QVariantMap m_value, m_pendingValue, m_layoutHint;
    qreal m_layoutWidth = 0;
    bool m_holdLayout = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
