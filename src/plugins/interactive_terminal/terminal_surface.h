#pragma once
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <memory>
class QMimeData;

namespace exagent {
class TerminalSurface : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(bool supported READ supported CONSTANT)
public:
  explicit TerminalSurface(QQuickItem *parent = nullptr);
  ~TerminalSurface() override;
  QString target() const { return m_target; }
  QString error() const { return m_error; }
  bool supported() const;
  void setTarget(const QString &id);
  void paint(QPainter *painter) override;
  Q_INVOKABLE void copySelection();
  Q_INVOKABLE void pasteClipboard();
  Q_INVOKABLE void copyLink();
  Q_INVOKABLE void openLink();
  Q_INVOKABLE void insertFiles(const QList<QUrl> &urls);
  Q_PROPERTY(QString contextLink READ contextLink NOTIFY contextLinkChanged)
  QString contextLink() const { return m_contextLink; }
signals:
  void contextLinkChanged();
  void contextMenuRequested(qreal x, qreal y);
  void targetChanged();
  void errorChanged();
  void pasteRejected(QString reason);

protected:
  void geometryChange(const QRectF &next, const QRectF &old) override;
  void keyPressEvent(QKeyEvent *event) override;
  void inputMethodEvent(QInputMethodEvent *event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void hoverMoveEvent(QHoverEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  QString lineText(int row, int start = 0, int end = -1) const;
  QString linkAt(const QPointF &position) const;
  void pasteText(QString text);
  void insertMimeData(const QMimeData *mime);
  QPoint cellAt(const QPointF &position) const;
  QPoint m_selectionStart{-1,-1}, m_selectionEnd{-1,-1};
  QString m_contextLink;
  struct Client;
  void resizeGrid();
  void fail(const QString &error);
  std::unique_ptr<Client> m_client;
  QString m_target, m_error, m_preedit;
  QFont m_font;
  int m_cw, m_ch, m_ascent;
};
} // namespace exagent
