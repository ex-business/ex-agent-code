#include "terminal_search.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
namespace exagent {
TerminalSearch::TerminalSearch(QObject *parent) : QObject(parent) {
  debounce.setSingleShot(true);
  debounce.setInterval(100);
}
void TerminalSearch::search(const QString &text, const QString &query,
                            bool matchCase, bool wholeWord) {
  const auto serial = ++generation;
  debounce.stop();
  disconnect(&debounce, nullptr, this, nullptr);
  matches.clear();
  index = -1;
  capped = false;
  working = !query.isEmpty();
  emit changed();
  if (query.isEmpty()) {
    emit ready();
    return;
  }
  // 快速输入只提交最后一次搜索；正文和结果都有上限，避免GUI线程反复扫描长回滚。
  connect(&debounce, &QTimer::timeout, this,
          [this, text, query, matchCase, wholeWord, serial] {
            auto *watcher = new QFutureWatcher<QVector<Match>>(this);
            connect(watcher, &QFutureWatcher<QVector<Match>>::finished, this,
                    [this, watcher, serial] {
                      auto result = watcher->result();
                      watcher->deleteLater();
                      if (serial != generation)
                        return;
                      capped = result.size() > 20000;
                      if (capped)
                        result.resize(20000);
                      matches = std::move(result);
                      working = false;
                      emit changed();
                      emit ready();
                    });
            watcher->setFuture(QtConcurrent::run([text, query, matchCase,
                                                  wholeWord] {
              QVector<Match> result;
              if (query.size() > 4096 || text.size() > 4 * 1024 * 1024)
                return result;
              const auto word = [](QChar c) {
                return c.isLetterOrNumber() || c == '_' ||
                       c.category() == QChar::Mark_NonSpacing ||
                       c.category() == QChar::Mark_SpacingCombining;
              };
              int pos = 0, line = 1, scanned = 0;
              while ((pos = text.indexOf(query, pos,
                                         matchCase ? Qt::CaseSensitive
                                                   : Qt::CaseInsensitive)) >=
                     0) {
                const auto end = pos + query.size();
                if (!wholeWord || ((pos == 0 || !word(text[pos - 1])) &&
                                   (end == text.size() || !word(text[end])))) {
                  while (scanned < pos) {
                    if (text[scanned++] == '\n')
                      ++line;
                  }
                  result.append({pos, int(query.size()), line});
                  if (result.size() > 20000)
                    break;
                }
                pos = end;
              }
              return result;
            }));
          });
  debounce.start();
}
QVariantMap TerminalSearch::step(bool forward) {
  if (working || matches.isEmpty())
    return {};
  const auto previous = index;
  index = index < 0
              ? (forward ? 0 : matches.size() - 1)
              : (index + (forward ? 1 : matches.size() - 1)) % matches.size();
  const auto m = matches[index];
  emit changed();
  return {{"start", m.start},
          {"length", m.length},
          {"line", m.line},
          {"wrapped",
           previous >= 0 && (forward ? index <= previous : index >= previous)}};
}
} // namespace exagent
