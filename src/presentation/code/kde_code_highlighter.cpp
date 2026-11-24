#include "kde_code_highlighter.h"
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>

namespace exagent {
namespace {
QString definitionName(const QString &language)
{
    if (language == "go") return QStringLiteral("Go");
    if (language == "c") return QStringLiteral("C");
    if (language == "shell") return QStringLiteral("Bash");
    return {};
}

}
bool KdeCodeHighlighter::supports(const QString &language) { return !definitionName(language).isEmpty(); }

KdeCodeHighlighter::KdeCodeHighlighter(const QString &language)
{
    // 索引线程各自复用语法库，避免在界面线程初始化或并发操作同一份语法定义。
    static thread_local KSyntaxHighlighting::Repository repository;
    setDefinition(repository.definitionForName(definitionName(language)));
    m_light = repository.theme(QStringLiteral("GitHub Light"));
    if (!m_light.isValid()) m_light = repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    m_dark = repository.theme(QStringLiteral("VSCodium Dark"));
    if (!m_dark.isValid()) m_dark = repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
}
void KdeCodeHighlighter::scan(QStringView line, int visibleLimit, QVector<CodeColorSpan> &spans)
{
    m_spans = &spans;
    m_limit = visibleLimit;
    // 即使单行显示被截断，也要解析行尾以确定下一行的注释和字符串状态。
    m_state = highlightLine(line, m_state);
    m_spans = nullptr;
}
void KdeCodeHighlighter::applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format)
{
    if (offset >= m_limit || length <= 0) return;
    if (!format.hasTextColor(m_light) && !format.hasTextColor(m_dark)) return;
    m_spans->append({offset, qMin(length, m_limit - offset), format.textColor(m_light).rgba(), format.textColor(m_dark).rgba()});
}
}
