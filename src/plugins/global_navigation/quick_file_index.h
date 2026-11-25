#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

namespace exagent {
class QuickFileIndex : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool limited READ limited NOTIFY changed)
public:
    using QObject::QObject;
    ~QuickFileIndex() override;
    bool loading() const { return m_loading; }
    bool limited() const { return m_limited; }
    Q_INVOKABLE void scan(const QString &root);
    Q_INVOKABLE QVariantList matches(const QString &query, const QVariantList &tabs) const;
signals:
    void changed();
private:
    QStringList m_files;
    qsizetype m_tracked=0;
    QString m_root;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_loading = false, m_limited = false;
};
}
