#pragma once
#include "code_color_span.h"
#include <QVector>
#include <atomic>
#include <memory>
#include <tree_sitter/api.h>
namespace exagent {
struct GoParseSession {
    QString source;
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree{nullptr, ts_tree_delete};
};
bool highlightGoReadOutput(const QString &code, QVector<CodeColorSpan> &spans, const std::atomic_bool &cancel);
bool highlightGoDocument(GoParseSession &session, const QString &code, QVector<CodeColorSpan> &spans, const std::atomic_bool &cancel, bool terminalOutput = false);
}
