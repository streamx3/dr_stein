// SPDX-License-Identifier: MIT
// The mounts the app made, for the Mounts popover and the "mounted at" notes.
// Also the watchdog for mounts that end outside the app (umount by hand, a
// Finder eject): every two seconds, while anything is mounted, records whose
// serving loop ended or whose mountpoint left the OS mount table are dropped.
#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QTimer>

#include <vector>

namespace drstein::ui {

class MountsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    enum Roles { Id = Qt::UserRole + 1, Mountpoint, What, Running, Error, SourceId };
    static MountsModel* instance();
    static MountsModel* create(QQmlEngine*, QJSEngine*);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool available() const;

    // Re-reads the records and tells the workspace, so rows and the details card follow.
    Q_INVOKABLE void reload();
    Q_INVOKABLE void unmount(int row);
    Q_INVOKABLE void unmountAll();
    // Opens the mountpoint in the OS's file manager (Finder, Explorer, the desktop's default).
    Q_INVOKABLE void reveal(int row);
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    explicit MountsModel(QObject* parent = nullptr);
    void reloadRows();   // the model alone, from the manager's records
    void poll();         // the watchdog tick
    struct Row {
        int id;
        QString mountpoint, what, error, sourceId;
        bool running;
    };
    std::vector<Row> m_rows;
    QTimer m_watchdog;
};

} // namespace drstein::ui
