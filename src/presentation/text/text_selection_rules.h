#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 正文和虚拟代码行共享 Unicode 选区规则，界面层只处理鼠标手势。
class TextSelectionRules final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(TextSelectionRules)
    QML_SINGLETON
    Q_PROPERTY(int doubleClickInterval READ doubleClickInterval CONSTANT)
    Q_PROPERTY(int dragDistance READ dragDistance CONSTANT)
public:
    explicit TextSelectionRules(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QVariantMap range(const QString &text, int position, int unit, bool code = false) const;
    int doubleClickInterval() const;
    int dragDistance() const;
};
}
