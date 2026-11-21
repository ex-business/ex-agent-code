#pragma once

#include <QAbstractListModel>
#include <QCache>
#include <QVector>
#include <QFont>
#include <QTextLayout>
#include <memory>
#include <QtQml/qqmlregistration.h>

namespace exagent {

class CodeLineModel : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(CodeLineModel)
    Q_PROPERTY(QString code READ code WRITE setCode NOTIFY codeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY codeChanged)
    Q_PROPERTY(int maximumColumns READ maximumColumns NOTIFY codeChanged)
    Q_PROPERTY(int cachedLineCount READ cachedLineCount NOTIFY metricsChanged)
    Q_PROPERTY(int formattedLineCount READ formattedLineCount NOTIFY metricsChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    Q_PROPERTY(int selectionLength READ selectionLength NOTIFY selectionChanged)
    Q_PROPERTY(int selectionAnchor READ selectionAnchor NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCursor READ selectionCursor NOTIFY selectionChanged)
public:
    enum Roles { PlainRole = Qt::UserRole + 1, HtmlRole, NumberRole, SelectionFromRole, SelectionToRole };
    explicit CodeLineModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString code() const;
    QString language() const;
    bool dark() const;
    int lineCount() const;
    int maximumColumns() const;
    int cachedLineCount() const;
    int formattedLineCount() const;
    void setCode(const QString &code);
    void setLanguage(const QString &language);
    void setDark(bool dark);
    QString selectedText() const;
    int selectionLength() const;
    int selectionAnchor() const;
    int selectionCursor() const;
    Q_INVOKABLE int positionAt(int row, int displayColumn) const;
    Q_INVOKABLE int positionAtPixel(int row, qreal x, const QFont &font, bool nearestCursor = true) const;
    Q_INVOKABLE QVariantMap cursorGeometry(const QFont &font) const;
    Q_INVOKABLE bool textAtPixel(int row, qreal x, const QFont &font) const;
    Q_INVOKABLE QVariantMap rangeAt(int position, int unit) const;
    Q_INVOKABLE void moveCursor(int key, bool extend, bool byWord, const QFont &font);
    Q_INVOKABLE void select(int anchor, int cursor);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectWord(int position);
signals:
    void codeChanged();
    void languageChanged();
    void darkChanged();
    void metricsChanged();
    void selectionChanged();
private:
    struct Line { qsizetype offset; qsizetype length; int state; };
    void rebuildIndex();
    void queueMetrics() const;
    int rowAt(int position) const;
    QTextLine layoutLine(int row, const QFont &font) const;
    QString m_code;
    QString m_language;
    bool m_dark = true;
    QVector<Line> m_lines;
    int m_maximumColumns = 0;
    int m_anchor = 0;
    int m_cursor = 0;
    qreal m_preferredX = -1;
    mutable int m_layoutRow = -1;
    mutable QFont m_layoutFont;
    mutable std::unique_ptr<QTextLayout> m_hitLayout;
    mutable QCache<int, QString> m_highlightCache{2048};
    mutable int m_formattedLineCount = 0;
    mutable bool m_metricsPending = false;
};

} // namespace exagent
