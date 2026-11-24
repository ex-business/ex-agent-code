#pragma once
#include <QColor>
#include <QByteArray>
namespace exagent {
struct TokenColors { QRgb light, dark; };
TokenColors tokenColors(const QByteArray &scope);
}
