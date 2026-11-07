#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

// A list of QVariantMaps exposed to QML with one role per key. Small, generic, fast enough
// for the list sizes here (hundreds to a few thousand rows) and keeps feature code compact.
class ListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit ListModel(const QStringList &roles, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return m_items.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return m_roleNames; }

    int count() const { return m_items.size(); }
    const QVector<QVariantMap> &items() const { return m_items; }
    void setItems(QVector<QVariantMap> items);
    void append(const QVariantMap &item);
    void prepend(const QVariantMap &item);
    void setItem(int row, const QVariantMap &item);
    void setField(int row, const QString &key, const QVariant &value);
    void removeAt(int row);
    int indexOf(const QString &key, const QVariant &value) const;
    Q_INVOKABLE QVariantMap get(int row) const { return row >= 0 && row < m_items.size() ? m_items[row] : QVariantMap{}; }

Q_SIGNALS:
    void countChanged();

private:
    QStringList m_roles;
    QHash<int, QByteArray> m_roleNames;
    QVector<QVariantMap> m_items;
};
