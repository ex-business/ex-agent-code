#pragma once
#include <QObject>
#include <QVariantList>
#include <QFont>
#include <QtQml/qqmlregistration.h>
namespace exagent {
class MarkdownFormatter final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(MarkdownFormatter)
    QML_SINGLETON
public:
    explicit MarkdownFormatter(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList split(const QString &content) const;
    QVariantMap prosePresentation(const QString &content, bool dark) const;
    Q_INVOKABLE QString pythonPreview(const QString &command) const;
    Q_INVOKABLE QString shellPreview(const QString &command, bool dark) const;
    Q_INVOKABLE QVariantList format(const QString &content, bool dark = false) const;
    Q_INVOKABLE QVariantMap userPresentation(const QString &content, const QFont &font) const;
};
}
