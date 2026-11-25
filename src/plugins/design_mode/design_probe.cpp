#include "design_probe.h"
#include <QFile>
#include <cmath>

namespace exagent {
QString DesignProbe::script(double x, double y, bool details) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= 1 || y >= 1) return {};
    static const QString function = [] {
        QFile resource(":/design-mode/element_probe.js");
        return resource.open(QIODevice::ReadOnly) ? QString::fromUtf8(resource.readAll()) : QString{};
    }();
    if (function.isEmpty()) return {};
    return function + '(' + QString::number(x, 'g', 12) + ',' + QString::number(y, 'g', 12)
        + ',' + (details ? "true" : "false") + ')';
}
}
