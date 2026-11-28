#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>
namespace exagent {
class TerminalSearch : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(int current READ current NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool limited READ limited NOTIFY changed)
public:
  explicit TerminalSearch(QObject *parent = nullptr);
  int count() const { return matches.size(); }
  int current() const { return index + 1; }
  bool busy() const { return working; }
  bool limited() const { return capped; }
  Q_INVOKABLE void search(const QString &text, const QString &query,
                          bool matchCase, bool wholeWord);
  Q_INVOKABLE QVariantMap step(bool forward);
signals:
  void changed();
  void ready();

private:
  struct Match {
    int start, length, line;
  };
  QVector<Match> matches;
  int index = -1;
  quint64 generation = 0;
  bool working = false, capped = false;
  QTimer debounce;
};
} // namespace exagent
