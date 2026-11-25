#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class DesignProbe : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE QString script(double x, double y, bool details) const;
};
}
