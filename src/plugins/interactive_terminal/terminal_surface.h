#pragma once
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include <memory>

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
signals:
  void targetChanged();
  void errorChanged();
  void pasteRejected(QString reason);

protected:
  void geometryChange(const QRectF &next, const QRectF &old) override;
  void keyPressEvent(QKeyEvent *event) override;
  void inputMethodEvent(QInputMethodEvent *event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void mousePressEvent(QMouseEvent *event) override;

private:
  struct Client;
  void resizeGrid();
  void fail(const QString &error);
  std::unique_ptr<Client> m_client;
  QString m_target, m_error, m_preedit;
  QFont m_font;
  int m_cw, m_ch, m_ascent;
};
} // namespace exagent
