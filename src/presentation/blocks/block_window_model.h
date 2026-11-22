#pragma once
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace exagent {
// 独立窗口按稳定身份逐行增删，不能用整个QVariantList重置所有窗口。
class BlockWindowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit BlockWindowModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_windows.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.size()) return {};
        return role == Qt::UserRole + 1 ? m_windows[index.row()].first : role == Qt::UserRole + 2 ? m_windows[index.row()].second : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "blockId"}, {Qt::UserRole + 2, "viewId"}}; }
    void add(const QString &id, const QString &view) {
        const int row = m_windows.size(); beginInsertRows({}, row, row); m_windows.append({id, view}); endInsertRows();
    }
    void remove(const QString &id) {
        for (int row = 0; row < m_windows.size(); ++row) if (m_windows[row].first == id) {
            beginRemoveRows({}, row, row); m_windows.removeAt(row); endRemoveRows(); return;
        }
    }
private:
    QList<QPair<QString, QString>> m_windows;
};
}
