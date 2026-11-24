#include "block_view_registry.h"
#include <QUuid>
#include <cmath>
#include <QJsonDocument>

namespace exagent {
namespace {
// 内容和任务状态跟随后台快照；命令、语言和当前标签属于打开窗口时的展示上下文。
QVariantMap windowSnapshot(const QVariantMap &stored, const QVariantMap &display)
{
    if (stored.isEmpty()) return display;
    QVariantMap result = stored;
    for (const auto key : {"shell", "command", "language", "initialPage", "messageStatus"})
        if (!stored.contains(key) && display.contains(key)) result.insert(key, display.value(key));
    return result;
}
}
BlockViewRegistry::BlockViewRegistry(ContentBlockStore &store, QObject *parent) : QObject(parent), m_store(store), m_windows(this)
{
    connect(&store, &ContentBlockStore::blockChanged, this, [this](const QString &id) {
        auto it = m_views.find(id);
        if (it == m_views.end() || it->placement != 2) return;
        const auto next = m_store.snapshot(id);
        if (!next.isEmpty()) { it->block = windowSnapshot(next, it->block); touch(); }
    });
}
void BlockViewRegistry::touch() { ++m_revision; emit changed(); }
BlockViewRegistry::View *BlockViewRegistry::ensure(const QString &id)
{
    if (!content::identifier(id)) return nullptr;
    if (!m_views.contains(id) && m_views.size() >= 512) { emit failure(QStringLiteral("块视图状态容量已满")); return nullptr; }
    return &m_views[id];
}
QVariantMap BlockViewRegistry::state(const QString &id) const
{
    const auto v = m_views.value(id);
    return {{"placement", v.placement}, {"expansion", v.expansion}, {"visibility", v.visibility}};
}
QVariantList BlockViewRegistry::detachedBlocks() const
{
    QVariantList result;
    for (auto it = m_views.cbegin(); it != m_views.cend(); ++it)
        if (it->placement == 2) result.append(QVariantMap{{"blockId", it.key()}, {"viewId", it->viewId}});
    return result;
}
QVariantMap BlockViewRegistry::snapshot(const QString &id) const { return m_views.value(id).block; }
void BlockViewRegistry::detach(const QVariantMap &fallback)
{
    const auto id = fallback.value("blockId").toString();
    auto block = windowSnapshot(m_store.snapshot(id), fallback);
    if (!content::identifier(id) || !content::integer(QJsonValue::fromVariant(block.value("rendererKind")), 1, 7)) return;
    if (QJsonDocument::fromVariant(block).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024) {
        emit failure(QStringLiteral("独立视图快照过大，请先导出内容")); return;
    }
    if (detachedBlocks().size() >= 16 && m_views.value(id).placement != 2) { emit failure(QStringLiteral("独立内容窗口最多16个")); return; }
    auto *view = ensure(id); if (!view) return;
    // 重复点击预览只唤回已有窗口，不重建播放器或丢失呈现状态。
    if (view->placement == 2) { emit focusRequested(id); return; }
    view->placement = 2; view->visibility = 1; view->block = block;
    view->viewId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    touch(); m_windows.add(id, view->viewId); emit viewsChanged();
}
void BlockViewRegistry::reattach(const QString &id)
{
    auto it = m_views.find(id); if (it == m_views.end() || it->placement != 2) return;
    it->placement = 1; it->visibility = 1; it->block.clear();
    // 只关闭呈现窗口，不取消任务；播放位置保留，返回内嵌后从同一位置显示。
    touch(); m_windows.remove(id); emit viewsChanged();
}
void BlockViewRegistry::setExpanded(const QString &id, int expansion)
{
    if (expansion != 1 && expansion != 2) return;
    if (auto *view = ensure(id)) { view->expansion = expansion; touch(); }
}
void BlockViewRegistry::closeInline(const QString &id) { if (auto *view = ensure(id)) { view->visibility = 2; touch(); } }
void BlockViewRegistry::reopen(const QString &id) { if (auto *view = ensure(id)) { view->visibility = 1; view->expansion = 1; touch(); } }
QVariantMap BlockViewRegistry::presentation(const QString &id) const { return m_views.value(id).presentation; }
void BlockViewRegistry::savePresentation(const QString &id, const QVariantMap &value)
{
    // 呈现状态只收有限的数值，不能让播放器把帧图片或任意对象塞入状态仓库。
    QVariantMap safe;
    for (const auto &key : {"frameIndex", "speed", "loopMode", "playbackState"}) {
        bool valid = false;
        const auto v = value.value(QLatin1String(key)).toDouble(&valid);
        const auto name = QLatin1String(key);
        if (!valid || !qIsFinite(v)) continue;
        const bool allowed = name == QLatin1String("frameIndex") ? (v >= 0 && v <= 499 && std::floor(v) == v)
            : name == QLatin1String("speed") ? (v == 0.5 || v == 1 || v == 2)
            : name == QLatin1String("loopMode") ? (v == 1 || v == 2) : (v == 1 || v == 2 || v == 3);
        if (allowed) safe.insert(name, v);
    }
    if (auto *view = ensure(id)) view->presentation = safe;
}
}
