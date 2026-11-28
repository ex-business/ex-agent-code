#pragma once
#include <QVariantList>
namespace exagent {
// 工作区专用源码行映射，不改变聊天 Markdown 的渲染契约。
QVariantList prepareWorkspaceMarkdown(const QString &source, bool dark);
} // namespace exagent
