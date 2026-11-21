#include "selection_coordinator.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QVariant>

namespace exagent {
bool SelectionCoordinator::hasSelection() const { return m_owner && m_owner->property("selectionLength").toInt() > 0; }
void SelectionCoordinator::activate(QObject *owner)
{
    if (m_owner == owner) return;
    disconnect(m_destroyed);
    const auto previous = m_owner;
    m_owner = owner;
    if (previous) QMetaObject::invokeMethod(previous, "clearSelection");
    if (owner) m_destroyed = connect(owner, &QObject::destroyed, this, [this] { m_owner = nullptr; emit selectionChanged(); });
    emit selectionChanged();
}
void SelectionCoordinator::changed(QObject *owner) { if (m_owner == owner) emit selectionChanged(); }
void SelectionCoordinator::copy()
{
    if (!hasSelection()) return;
    QVariant result;
    // 只有实际复制时才生成正文；拖动选区时仅传递长度与拥有者。
    if (QMetaObject::invokeMethod(m_owner, "selectedContent", Q_RETURN_ARG(QVariant, result))) {
        auto text = result.toString();
        text.replace(QChar::ParagraphSeparator, '\n');
        QGuiApplication::clipboard()->setText(text);
    }
}
}
