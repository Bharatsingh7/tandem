#include "listmodel.h"

ListModel::ListModel(const QStringList &roles, QObject *parent) : QAbstractListModel(parent), m_roles(roles) {
    for (int i = 0; i < roles.size(); ++i) m_roleNames[Qt::UserRole + 1 + i] = roles[i].toUtf8();
}

QVariant ListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_items.size()) return {};
    const int i = role - Qt::UserRole - 1;
    return i >= 0 && i < m_roles.size() ? m_items[index.row()].value(m_roles[i]) : QVariant{};
}

void ListModel::setItems(QVector<QVariantMap> items) {
    const bool sizeChanged = items.size() != m_items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (sizeChanged) Q_EMIT countChanged();
}

void ListModel::append(const QVariantMap &item) {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();
    Q_EMIT countChanged();
}

void ListModel::prepend(const QVariantMap &item) {
    beginInsertRows({}, 0, 0);
    m_items.prepend(item);
    endInsertRows();
    Q_EMIT countChanged();
}

void ListModel::setItem(int row, const QVariantMap &item) {
    if (row < 0 || row >= m_items.size()) return;
    m_items[row] = item;
    Q_EMIT dataChanged(index(row), index(row));
}

void ListModel::setField(int row, const QString &key, const QVariant &value) {
    if (row < 0 || row >= m_items.size()) return;
    m_items[row][key] = value;
    const int r = m_roles.indexOf(key);
    Q_EMIT dataChanged(index(row), index(row), r >= 0 ? QList<int>{Qt::UserRole + 1 + r} : QList<int>{});
}

void ListModel::removeAt(int row) {
    if (row < 0 || row >= m_items.size()) return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

int ListModel::indexOf(const QString &key, const QVariant &value) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].value(key) == value) return i;
    return -1;
}
