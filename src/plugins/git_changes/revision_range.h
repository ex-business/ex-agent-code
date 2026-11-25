#pragma once
#include "git_read.h"

namespace exagent {
struct RevisionRange { int scope; QString from, to; };
struct ChangedPath { QString path; char code; bool untracked; };
struct Comparison {
    QStringList revisions;
    QList<ChangedPath> files;
    QString error;
    bool limited = false;
};
Comparison compareRevisions(const QString &folder, const RevisionRange &range, const gitread::Cancel &cancel);
}
