#pragma once

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include "code_color_span.h"
#include <QVector>

namespace exagent {
class KdeCodeHighlighter final : public KSyntaxHighlighting::AbstractHighlighter {
public:
    explicit KdeCodeHighlighter(const QString &language);
    static bool supports(const QString &language);
    void scan(QStringView line, int visibleLimit, QVector<CodeColorSpan> &spans);
private:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;
    KSyntaxHighlighting::State m_state;
    KSyntaxHighlighting::Theme m_light, m_dark;
    QVector<CodeColorSpan> *m_spans = nullptr;
    int m_limit = 0;
};
}
