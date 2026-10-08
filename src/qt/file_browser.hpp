// SPDX-License-Identifier: MIT
// The Browse view's facade: a core::Browser over the selected node, the
// current directory as a list, a lazy directory tree, previews and jobs
// (hash, copy out, mount).
#pragma once

#include "drstein/core/browse.hpp"

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

#include <memory>
#include <optional>
#include <vector>

namespace drstein::ui {

class EntriesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by FileBrowser")
public:
    enum Roles { Name = Qt::UserRole + 1, Kind, SizeText, MtimeText, ModeText, LinkTarget, IsDir, Selected };
    explicit EntriesModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_entries.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void set(std::vector<core::Entry> entries);
    const std::vector<core::Entry>& entries() const { return m_entries; }
    void setSelected(int row);
    int selected() const { return m_selected; }

private:
    std::vector<core::Entry> m_entries;
    int m_selected = -1;
};

// Directories only, loaded when expanded. Rows carry the directory's absolute path.
class DirTreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by FileBrowser")
public:
    enum Roles { Name = Qt::UserRole + 1, Path, IsCurrent };
    explicit DirTreeModel(QObject* parent = nullptr) : QAbstractItemModel(parent) {}
    void reset(core::Browser* browser);
    void setCurrentPath(const QString& path);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return 1; }
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QModelIndex indexForPath(const QString& path);   // loads ancestors as needed

private:
    struct Node {
        QString name, path;
        stein::fs::Inode inode;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        bool loaded = false;
        bool hasDirs = true;
        int row = 0;
    };
    Node* nodeOf(const QModelIndex& i) const { return i.isValid() ? static_cast<Node*>(i.internalPointer()) : m_root.get(); }
    void load(Node* n);
    QModelIndex indexOf(Node* n) const;

    core::Browser* m_browser = nullptr;
    std::unique_ptr<Node> m_root;
    QString m_currentPath;
};

class FileBrowser : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString fsName READ fsName NOTIFY changed)
    Q_PROPERTY(QString pathText READ pathText NOTIFY changed)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY changed)
    Q_PROPERTY(EntriesModel* entries READ entries CONSTANT)
    Q_PROPERTY(DirTreeModel* tree READ tree CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList subvolumes READ subvolumes NOTIFY changed)
    Q_PROPERTY(QString subvolume READ subvolume NOTIFY changed)
    Q_PROPERTY(QString snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canMount READ canMount NOTIFY changed)
    // Selection and preview.
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedInfo READ selectedInfo NOTIFY selectionChanged)
    Q_PROPERTY(QString previewKind READ previewKind NOTIFY selectionChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY selectionChanged)
    Q_PROPERTY(QString previewHex READ previewHex NOTIFY selectionChanged)
    Q_PROPERTY(QString previewImage READ previewImage NOTIFY selectionChanged)
    Q_PROPERTY(QString previewNote READ previewNote NOTIFY selectionChanged)
    Q_PROPERTY(QString mtimeText READ mtimeText NOTIFY selectionChanged)
    Q_PROPERTY(QString crtimeText READ crtimeText NOTIFY selectionChanged)
    Q_PROPERTY(QString hashText READ hashText NOTIFY selectionChanged)
    Q_PROPERTY(bool showHex READ showHex WRITE setShowHex NOTIFY selectionChanged)

public:
    explicit FileBrowser(QObject* parent = nullptr);

    bool available() const { return m_browser.has_value(); }
    QString unavailableReason() const { return m_reason; }
    QString title() const;
    QString fsName() const;
    QString pathText() const;
    QVariantList breadcrumbs() const;
    EntriesModel* entries() { return &m_entries; }
    DirTreeModel* tree() { return &m_tree; }
    QString statusText() const;
    QVariantList subvolumes() const;
    QString subvolume() const { return m_subvolume; }
    QString snapshot() const { return m_snapshot; }
    bool busy() const { return m_busy; }
    bool canMount() const;
    int selectedIndex() const { return m_entries.selected(); }
    bool hasSelection() const { return m_entries.selected() >= 0; }
    QString selectedName() const;
    QString selectedInfo() const;
    QString previewKind() const { return m_previewKind; }
    QString previewText() const { return m_previewText; }
    QString previewHex() const { return m_previewHex; }
    QString previewImage() const { return m_previewImage; }
    QString previewNote() const { return m_previewNote; }
    QString mtimeText() const;
    QString crtimeText() const;
    QString hashText() const { return m_hashText; }
    bool showHex() const { return m_showHex; }
    void setShowHex(bool on);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void openSubvolume(const QString& volume, const QString& snapshot);
    Q_INVOKABLE void activate(int row);       // directory: enter; file: select
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void enter(const QString& name);
    Q_INVOKABLE void up();
    Q_INVOKABLE void jumpTo(int crumb);
    Q_INVOKABLE void goTo(const QString& path);
    Q_INVOKABLE void hash();
    Q_INVOKABLE void copyOut(int row, const QUrl& directory);
    Q_INVOKABLE void copyAll(const QUrl& directory);
    Q_INVOKABLE void mount();
    // Drag-out: copies the rows into a fresh staging folder under the temp directory and
    // returns their file URLs for a text/uri-list drag. Refuses (empty list, toast) above
    // `kDragLimit` bytes of regular files; folders are copied as they are. Staging folders
    // are removed by cleanupStaging() at quit, never earlier: the drop target may still be reading.
    Q_INVOKABLE QStringList stageForDrag(const QList<int>& rows);
    static void cleanupStaging();
    static constexpr quint64 kDragLimit = 1ull << 30;

Q_SIGNALS:
    void changed();
    void selectionChanged();
    void mounted(QString mountpoint);

private:
    void listCurrent();
    void updatePreview();
    void setBusy(bool b);

    std::optional<core::Browser> m_browser;
    EntriesModel m_entries;
    DirTreeModel m_tree;
    QString m_reason, m_subvolume, m_snapshot;
    QString m_previewKind = "none", m_previewText, m_previewHex, m_previewImage, m_previewNote, m_hashText;
    bool m_showHex = false, m_busy = false;
    core::NodePath m_path;
};

} // namespace drstein::ui
