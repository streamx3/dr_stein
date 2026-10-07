// SPDX-License-Identifier: MIT
// The mounts the app made, for the Mounts popover and the "mounted at" notes.
#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

#include <vector>

namespace drstein::ui {

class MountsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    enum Roles { Id = Qt::UserRole + 1, Mountpoint, What, Running, Error };
    static MountsModel* instance();
    static MountsModel* create(QQmlEngine*, QJSEngine*);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool available() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void unmount(int row);
    Q_INVOKABLE void unmountAll();

Q_SIGNALS:
    void countChanged();

private:
    explicit MountsModel(QObject* parent = nullptr);
    struct Row {
        int id;
        QString mountpoint, what, error;
        bool running;
    };
    std::vector<Row> m_rows;
};

} // namespace drstein::ui
