#pragma once

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml/qqmlregistration.h>

namespace exagent {
class AppearanceRegistry final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList themes READ themes CONSTANT)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap strings READ strings NOTIFY languageChanged)
    using Packs = QMap<QString,QJsonObject>;
    Packs m_themes, m_languages;
    QString m_theme, m_language;
    QSettings settings;
    static QVariantList options(const Packs &packs) {
        QVariantList list;
        for (const auto &pack : packs)
            list.append(QVariantMap{{"id",pack.value("id").toString()},{"name",pack.value("name").toString()}});
        return list;
    }
    void discover(const QString &path) {
        const QDir directory(path);
        for (const auto &name : directory.entryList({"*.json"}, QDir::Files, QDir::Name)) {
            QFile file(directory.filePath(name));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 2*1024*1024) continue;
            const auto pack = QJsonDocument::fromJson(file.readAll()).object();
            const auto id = pack.value("id").toString();
            if (pack.value("version").toInt() != 1 || id.isEmpty() || pack.value("name").toString().isEmpty()) continue;
            // 内置包优先；扩展只提供数据，不能覆盖已有标识或执行脚本。
            if (pack.value("type") == "theme" && pack.value("dark").isBool() && pack.value("colors").isObject() && !m_themes.contains(id)) {
                const auto colors = pack.value("colors").toObject();
                bool valid = true;
                for (const auto &color : colors) valid &= QColor::isValidColorName(color.toString());
                if (valid) m_themes.insert(id,pack);
            } else if (pack.value("type") == "language" && pack.value("strings").isObject() && !m_languages.contains(id)) {
                bool valid = true;
                for (const auto &text : pack.value("strings").toObject()) valid &= text.isString();
                if (valid) m_languages.insert(id,pack);
            }
        }
    }
public:
    explicit AppearanceRegistry(QObject *parent=nullptr) : QObject(parent) {
        discover(":/appearance");
        discover(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins/appearance");
        m_theme = settings.value("appearance/theme","light").toString();
        m_language = settings.value("appearance/language","zh-CN").toString();
        if (!m_themes.contains(m_theme)) m_theme = "light";
        if (!m_languages.contains(m_language)) m_language = "zh-CN";
    }
    QVariantList themes() const { return options(m_themes); }
    QVariantList languages() const { return options(m_languages); }
    QString theme() const { return m_theme; }
    QString language() const { return m_language; }
    bool dark() const { return m_themes.value(m_theme).value("dark").toBool(); }
    QVariantMap colors() const {
        auto result = m_themes.value(dark() ? "dark" : "light").value("colors").toObject().toVariantMap();
        result.insert(m_themes.value(m_theme).value("colors").toObject().toVariantMap());
        return result;
    }
    QVariantMap strings() const { return m_languages.value(m_language).value("strings").toObject().toVariantMap(); }
    void setTheme(const QString &id) {
        if (!m_themes.contains(id) || m_theme == id) return;
        settings.setValue("appearance/theme",id); m_theme=id; emit themeChanged();
    }
    void setLanguage(const QString &id) {
        if (!m_languages.contains(id) || m_language == id) return;
        settings.setValue("appearance/language",id); m_language=id; emit languageChanged();
    }
signals:
    void themeChanged();
    void languageChanged();
};
}
