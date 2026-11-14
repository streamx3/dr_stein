// SPDX-License-Identifier: MIT
// The Browse view's facade: a core::Browser over the selected node, the
// current directory as a list, a lazy directory tree, the columns of the
// column layout, previews and jobs (hash, copy out, mount). A preview is read
// and decoded by a core::PreviewLoader on its own thread with its own reader;
// this thread only asks for one and shows what comes back.
#pragma once

#include "drstein/core/browse.hpp"
#include "drstein/core/columns.hpp"
#include "drstein/core/preview_loader.hpp"

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace drstein::ui {

struct PreviewContent;   // what the preview card shows of one file; made on the preview worker

// One directory's entries. The listing is shared, not copied: the current directory's with the
// column that shows it and with the listing cache.
class EntriesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by FileBrowser")
public:
    enum Roles { Name = Qt::UserRole + 1, Kind, SizeText, MtimeText, ModeText, LinkTarget, IsDir, Selected };
    explicit EntriesModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(entries().size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    void set(std::shared_ptr<const core::Listing> listing);   // null: no entries; nothing selected
    const std::shared_ptr<const core::Listing>& listing() const { return m_listing; }
    const std::vector<core::Entry>& entries() const;
    void setSelected(int row);
    int selected() const { return m_selected; }

private:
    std::shared_ptr<const core::Listing> m_listing;
    int m_selected = -1;
};

// The column layout's columns, left to right (core::planColumns). Every column has an
// EntriesModel of its own that stays with it while its directory does, so a column keeps its
// rows and scroll position when the current directory moves past it; the current column's
// shares the listing and the selected row of FileBrowser::entries.
class ColumnsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by FileBrowser")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    // selectedRow: the current column's selection, an ancestor's row on the way to the current
    // directory, -1 in the peek. error: why the directory could not be listed.
    enum Roles { Title = Qt::UserRole + 1, Current, Peek, Crumb, SelectedRow, Entries, EntryCount, Error };
    struct Column {
        core::ColumnPlan plan;
        QString title;                                  // the volume's title for the root
        std::shared_ptr<const core::Listing> listing;   // null when it could not be listed
        QString error;
        int selectedRow = -1;
    };

    explicit ColumnsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return count(); }
    int count() const { return static_cast<int>(m_columns.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

    std::vector<core::ColumnPlan> plans() const;
    const Column& at(std::size_t i) const { return m_columns[i].column; }
    void truncate(std::size_t n);              // removes the columns from n on
    void append(Column column);
    void update(std::size_t i, Column column);   // the same directory: its rows stay unless the listing is another
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    struct Slot {
        Column column;
        EntriesModel* entries = nullptr;   // a child of this model
    };
    std::vector<Slot> m_columns;
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
    // Empty unless Settings.browseLayout is "columns": the list layout reads nothing for it.
    Q_PROPERTY(drstein::ui::ColumnsModel* columns READ columns CONSTANT)
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
    ColumnsModel* columns() { return &m_columns; }
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
    // Copy out… in two steps around the folder dialog: what is copied is the row (-1: the current
    // directory whole) when the dialog opens. A disk read again before it is accepted copies
    // nothing, and says so: the row may hold another entry by then.
    Q_INVOKABLE void prepareCopy(int row);
    Q_INVOKABLE void copyPrepared(const QUrl& directory);
    Q_INVOKABLE void mount();
    // Drag-out: copies the rows into a fresh staging folder under the temp directory and
    // returns their file URLs for a text/uri-list drag. Refuses (empty list, toast) above
    // `kDragLimit` bytes of regular files; folders are copied as they are. Staging folders
    // are removed by cleanupStaging() at quit, never earlier: the drop target may still be reading.
    Q_INVOKABLE QStringList stageForDrag(const QList<int>& rows);
    static void cleanupStaging();
    static constexpr quint64 kDragLimit = 1ull << 30;

    // The column layout. The peek column follows the selection: at once after a click, after
    // the keyboard's pause (80 ms) after a key.
    // A click on a row: in the current column it selects; in an ancestor it goes up to that
    // column first, in the peek it enters the peek's folder first. A row of -1 (the column's
    // empty space) does only the first part.
    Q_INVOKABLE void columnClicked(int column, int row);
    Q_INVOKABLE void columnActivated(int column, int row);   // that click, then a folder is entered
    // stageForDrag for a row of any column; neither the selection nor the current directory changes.
    Q_INVOKABLE QStringList stageColumnForDrag(int column, int row);
    Q_INVOKABLE void enterSelectedAndSelectFirst();          // a folder: enter it, select its first entry

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
        core::PreviewTarget target;   // by path: the loader's reader may number files otherwise
    };
    struct CachedPreview {
        PreviewKey key;
        std::shared_ptr<const PreviewContent> content;
    };
    struct CopyTarget {
        quint64 generation = 0;
        stein::fs::Inode inode;
        std::string name;
    };

    // preview: false when a row is selected straight after, which then asks for the preview.
    void listCurrent(const std::string& reselect = {}, bool preview = true);
    stein::Expected<std::shared_ptr<const core::Listing>> readCurrent();
    // listed: the entry's inode when it was picked from a listing, which then decides; else by name.
    bool enterDirectory(const std::string& name, const stein::fs::Inode* listed = nullptr, bool preview = true);
    // previewStale: the preview shown is not the selection's even if the row did not move.
    void selectRow(int row, PreviewStart start, bool previewStale = false);
    const core::Entry* selectedEntry() const;
    void setBusy(bool b);
    QStringList stage(const std::vector<core::Entry>& picked);
    void startCopy(const stein::fs::Inode& source, const std::string& name, const QUrl& directory);
    std::optional<CopyTarget> copyTarget(int row) const;

    bool columnLayout() const;
    void layoutChanged();
    // The columns in line with the breadcrumbs, the selection and m_peekRow. retryFailed: a
    // column that could not be listed is read again (a click; never a key step).
    void syncColumns(bool retryFailed = false);
    ColumnsModel::Column columnFor(const core::ColumnPlan& plan, const ColumnsModel::Column* before, bool retryFailed);
    bool clickColumn(int column, int row);

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
    void uiLockChanged();
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
    quint64 m_generation = 0;                  // counts the readers m_browser had: an inode is of one of them
    std::optional<CopyTarget> m_copyTarget;    // prepareCopy()

    bool m_previewLoading = false;             // a preview of the selection is wanted and has not come
    bool m_previewHeld = false;                // ... and waits for a job to let go of the UI
    std::optional<PreviewRequest> m_request;   // what is wanted, from the first step of a keyboard move on
    quint64 m_ticket = 0;                      // the loader's ticket for it once it was asked; 0 = not asked
    // Tickets start again with every loader, so a result is told apart by the loader it came from too.
    quint64 m_epoch = 0;
    QString m_loaderError;                     // why this browser has no second reader; not retried per file
    QTimer m_debounce, m_grace, m_stall;
    std::deque<CachedPreview> m_cache;         // most recent first; emptied when the browser or the switch drops the loader

    // The column layout. Directories are listed on this thread through m_browser, never while
    // one of this object's jobs reads through it: m_columnsStale then waits for the job.
    ColumnsModel m_columns;
    core::ListingCache m_listings;             // of m_browser only: emptied whenever it is reopened
    QTimer m_peekTimer;
    int m_peekRow = -1;                        // the current column's row the peek column shows
    bool m_columnsStale = false;
    QString m_currentError;                    // why the current directory could not be listed
    // Last: it goes first, so nothing is delivered into a half-destroyed object. Its worker owns
    // a second reader of the browsed node and never touches m_browser.
    std::unique_ptr<core::PreviewLoader> m_loader;
};

} // namespace drstein::ui
