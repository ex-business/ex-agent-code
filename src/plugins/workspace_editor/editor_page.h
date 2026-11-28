#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QtQml/qqmlregistration.h>
namespace exagent {
class EditorDocument;
// 大文件仍由原文档拥有完整内容和撤销栈；界面只排版一个UTF-16窗口。
class EditorPage final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("由编辑文档创建")
  Q_PROPERTY(int number READ number NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(int firstLine READ firstLine NOTIFY changed)
  Q_PROPERTY(int lines READ lines NOTIFY changed)
  Q_PROPERTY(int cursor READ cursor NOTIFY windowChanged)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit EditorPage(EditorDocument *document);
  ~EditorPage() override;
  int number() const { return page + 1; }
  int count() const;
  int firstLine() const;
  int lines() const;
  int cursor() const { return localCursor; }
  QString error() const { return message; }
  Q_INVOKABLE void attach(QQuickTextDocument *view);
  Q_INVOKABLE void show(int number);
  Q_INVOKABLE void goToLine(int line);
  Q_INVOKABLE void recordCursor(int cursor);
  Q_INVOKABLE void locate(int global);
signals:
  void changed();
  void windowChanged();

private:
  void load(int index, int globalCursor = -1);
  void edited();
  static constexpr int Chunk = 32768;
  EditorDocument *document;
  QTextDocument window;
  QPointer<QQuickTextDocument> view;
  QString baseline, message;
  int page = 0, start = 0, localCursor = 0;
  bool updating = false;
};
} // namespace exagent
