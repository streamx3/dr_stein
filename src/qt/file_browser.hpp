// SPDX-License-Identifier: MIT
// The Browse view's facade: a core::Browser over the selected node, the
// current directory as a list, a lazy directory tree, previews and jobs
// (hash, copy out, mount). A preview is read and decoded by a
// core::PreviewLoader on its own thread with its own reader; this thread only
// asks for one and shows what comes back.
#pragma once

#include "drstein/core/browse.hpp"
#include "drstein/core/preview_loader.hpp"

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace drstein::ui {

struct PreviewContent;   // what the preview card shows of one file; made on the preview worker

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
    Q_PROPERTY(drstein::ui::EntriesModel* entries READ entries CONSTANT)
    Q_PROPERTY(drstein::ui::DirTreeModel* tree READ tree CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectionChanged)   // counts the entries and the selection
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
    // "none" (no selection), "off" (previews are off and nothing was read), "loading", "empty",
    // "text", "hex", "image", "unreadable". While previewLoading is true the kind and the content
    // are still the previous file's for a moment (150 ms), then the kind turns "loading"; when
    // it is a running job the preview waits for, previewNote says so.
    Q_PROPERTY(QString previewKind READ previewKind NOTIFY selectionChanged)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY selectionChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY selectionChanged)
    Q_PROPERTY(QString previewHex READ previewHex NOTIFY selectionChanged)
    Q_PROPERTY(QImage previewImage READ previewImage NOTIFY selectionChanged)
    Q_PROPERTY(QString previewNote READ previewNote NOTIFY selectionChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled NOTIFY selectionChanged)   // Settings.previewFiles
    Q_PROPERTY(QString mtimeText READ mtimeText NOTIFY selectionChanged)
    Q_PROPERTY(QString crtimeText READ crtimeText NOTIFY selectionChanged)
    Q_PROPERTY(QString hashText READ hashText NOTIFY selectionChanged)
    Q_PROPERTY(bool showHex READ showHex WRITE setShowHex NOTIFY selectionChanged)

public:
    explicit FileBrowser(QObject* parent = nullptr);
    ~FileBrowser() override;

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
    bool previewLoading() const { return m_previewLoading; }
    QString previewText() const { return m_previewText; }
    QString previewHex() const { return m_previewHex; }
    QImage previewImage() const { return m_previewImage; }
    QString previewNote() const { return m_previewNote; }
    bool previewEnabled() const;
    QString mtimeText() const;
    QString crtimeText() const;
    QString hashText() const { return m_hashText; }
    bool showHex() const { return m_showHex; }
    void setShowHex(bool on);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void openSubvolume(const QString& volume, const QString& snapshot);
    Q_INVOKABLE void activate(int row);       // directory: enter; file: select
    Q_INVOKABLE void select(int row);         // a click: the preview is asked for at once
    // The keyboard's way through the list: the row moves at once, the preview follows once the
    // movement pauses (80 ms), so a held arrow key skims without a read per row.
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void selectFirst();
    Q_INVOKABLE void selectLast();
    Q_INVOKABLE void activateSelected();      // directory: enter; file: nothing
    Q_INVOKABLE void previewOnce();           // the selected file, even with previews off
    Q_INVOKABLE void enter(const QString& name);
    Q_INVOKABLE void up();                    // both select the directory that was left
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
    enum class PreviewStart { Now, Debounced, Forced };   // Forced: previewOnce(), which the switch does not stop
    struct PreviewKey {
        std::uint64_t inode = 0, size = 0;
        std::int64_t mtime = 0;
        bool operator==(const PreviewKey&) const = default;
    };
    struct PreviewRequest {
        PreviewKey key;
        stein::fs::Inode inode;
        stein::fs::Stat stat;
    };
    struct CachedPreview {
        PreviewKey key;
        std::shared_ptr<const PreviewContent> content;
    };

    void listCurrent(const std::string& reselect = {});
    void selectRow(int row, PreviewStart start);
    const core::Entry* selectedEntry() const;
    void setBusy(bool b);

    void refreshPreview(PreviewStart start);             // brings the preview in line with the selection; reads nothing
    void settlePreview(const PreviewContent& content);   // the selection's preview is known: nothing stays asked for
    void showLoading(const QString& note = {});
    void sendRequest();
    void cancelTicket();
    bool ensureLoader();
    void dropLoader();
    bool previewHeld() const;
    void previewArrived(quint64 epoch, quint64 ticket, std::shared_ptr<const PreviewContent> content, const QString& failure);
    void graceEnded();
    void requestStalled();
    void previewSettingChanged();
    std::shared_ptr<const PreviewContent> cachedPreview(const PreviewKey& key);
    void rememberPreview(const PreviewKey& key, std::shared_ptr<const PreviewContent> content);

    std::optional<core::Browser> m_browser;
    EntriesModel m_entries;
    DirTreeModel m_tree;
    QString m_reason, m_subvolume, m_snapshot;
    QString m_previewKind = "none", m_previewText, m_previewHex, m_previewNote, m_hashText;
    QImage m_previewImage;
    bool m_showHex = false, m_busy = false;
    core::NodePath m_path;
    // A reload asked for while one of this object's jobs reads through m_browser on the job
    // thread: volume and snapshot, opened when the job ends.
    std::optional<std::pair<QString, QString>> m_reopen;

    bool m_previewLoading = false;             // a preview of the selection is wanted and has not come
    bool m_previewHeld = false;                // ... and waits for a job to let go of the UI
    std::optional<PreviewRequest> m_request;   // what is wanted, from the first step of a keyboard move on
    quint64 m_ticket = 0;                      // the loader's ticket for it once it was asked; 0 = not asked
    // Tickets start again with every loader, so a result is told apart by the loader it came from too.
    quint64 m_epoch = 0;
    // The worker may still be inside a request that was given up: a read on failing media or a
    // decode, neither of which can be interrupted. Cleared by the next result.
    bool m_workerMayLag = false;
    int m_abandonedWorkers = 0;                // loaders replaced for that, for this browser
    QString m_loaderError;                     // why this browser has no second reader; not retried per file
    QTimer m_debounce, m_grace, m_stall;
    std::deque<CachedPreview> m_cache;         // most recent first; emptied when the browser or the switch drops the loader
    // Last: it goes first, so nothing is delivered into a half-destroyed object. Its worker owns
    // a second reader of the browsed node and never touches m_browser.
    std::unique_ptr<core::PreviewLoader> m_loader;
};

} // namespace drstein::ui
