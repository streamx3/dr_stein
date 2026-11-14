// SPDX-License-Identifier: MIT
#include "file_browser.hpp"

#include "drstein/core/format.hpp"
#include "job_runner.hpp"
#include "mounts_model.hpp"
#include "preview_decode.hpp"
#include "settings.hpp"
#include "stein/core/strings.hpp"
#include "util.hpp"
#include "workspace.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace drstein::ui {

// ---- The preview worker's half ---------------------------------------------------------

struct PreviewContent {
    QString kind = "none", text, hex, note;
    QImage image;
    bool remember = true;   // false when what failed was of the moment, not of the file
};

namespace {

constexpr int kDebounceMs = 80;   // a pause this long in keyboard movement asks for the preview
constexpr int kGraceMs = 150;     // the previous preview stays up this long while the next one loads
// A request that waits this long behind a decode that was given up gets a new worker: a decode
// cannot be interrupted, and would hold every later preview back. A read that hangs is waited
// out instead: every reader of the disk would queue behind it, a new one included.
constexpr int kStallMs = 10000;
// Workers alive in the process, the ones left behind in a decode included, beyond which no
// worker is left behind for another: each holds a reader and what it decodes.
constexpr std::size_t kMaxPreviewWorkers = 3;
constexpr std::size_t kHexBytes = 4096;
constexpr std::size_t kCacheEntries = 12;
constexpr qsizetype kCacheBytes = 96 * 1024 * 1024;

qsizetype costOf(const PreviewContent& c) { return c.image.sizeInBytes() + (c.text.size() + c.hex.size()) * static_cast<qsizetype>(sizeof(QChar)); }

QString kindName(core::PreviewKind kind) {
    using core::PreviewKind;
    switch (kind) {
    case PreviewKind::Empty: return "empty";
    case PreviewKind::Text: return "text";
    case PreviewKind::Hex: return "hex";
    case PreviewKind::Image: return "image";
    case PreviewKind::Unreadable: return "unreadable";
    }
    return "empty";
}

// PreviewLoader's transform: on the worker, from what was read to what is shown, so that the
// decode and the string conversions stay off the GUI thread and the file's bytes never reach
// it. A free function on purpose: it may still run when the FileBrowser is gone.
std::shared_ptr<void> renderPreview(const core::Preview& pv) {
    auto out = std::make_shared<PreviewContent>();
    out->kind = kindName(pv.kind);
    out->note = qs(pv.note);
    if (pv.kind == core::PreviewKind::Text) out->text = qs(pv.text);
    if (!pv.bytes.empty()) {
        const std::size_t n = std::min(pv.bytes.size(), kHexBytes);
        out->hex = qs(stein::hexDump(std::span<const std::byte>(pv.bytes.data(), n)));
        if (pv.bytes.size() > n) out->hex += "\n… first 4 KiB shown";
    }
    if (pv.kind != core::PreviewKind::Image) return out;

    // imageFormat goes through as core set it: "svg" there means the text was vetted.
    const DecodedPreview decoded = decodePreview(pv.bytes, qs(pv.imageFormat));
    if (decoded.image.isNull()) {
        // The source of an SVG says more than its bytes in hex.
        out->kind = pv.text.empty() ? "hex" : "text";
        out->text = qs(pv.text);
        out->note = decoded.problem;
        out->remember = !decoded.retry;
        if (!pv.text.empty() && !pv.textNote.empty()) out->note += " · " + qs(pv.textNote);
    } else {
        out->image = decoded.image;
        out->note = qs(core::imageFormatLabel(decoded.format.isEmpty() ? pv.imageFormat : ss(decoded.format)));
        if (!decoded.sourceSize.isEmpty()) out->note += QString(" · %1 × %2").arg(decoded.sourceSize.width()).arg(decoded.sourceSize.height());
    }
    if (!pv.note.empty()) out->note += " · " + qs(pv.note);
    return out;
}

} // namespace

// ---- EntriesModel -------------------------------------------------------------------

const std::vector<core::Entry>& EntriesModel::entries() const {
    static const std::vector<core::Entry> none;
    return m_listing ? m_listing->entries : none;
}

QVariant EntriesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& e = entries()[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Name: return qs(e.entry.name);
    case Kind: {
        using stein::fs::FileType;
        switch (e.stat.type) {
        case FileType::Directory: return "dir";
        case FileType::Symlink: return "link";
        case FileType::File: return "file";
        default: return "special";
        }
    }
    case SizeText: return qs(e.sizeText);
    case MtimeText: return qs(e.mtimeText);
    case ModeText: return qs(e.modeText);
    case LinkTarget: return e.linkTarget.empty() ? QString() : "→ " + qs(e.linkTarget);
    case IsDir: return e.isDir();
    case Selected: return index.row() == m_selected;
    }
    return {};
}

QHash<int, QByteArray> EntriesModel::roleNames() const {
    return {{Name, "name"}, {Kind, "kind"}, {SizeText, "sizeText"}, {MtimeText, "mtimeText"}, {ModeText, "modeText"},
            {LinkTarget, "linkTarget"}, {IsDir, "isDir"}, {Selected, "selected"}};
}

QVariantMap EntriesModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

void EntriesModel::set(std::shared_ptr<const core::Listing> listing) {
    beginResetModel();
    m_listing = std::move(listing);
    m_selected = -1;
    endResetModel();
}

void EntriesModel::setSelected(int row) {
    if (row < -1 || row >= rowCount()) row = -1;
    if (row == m_selected) return;
    const int before = m_selected;
    m_selected = row;
    if (before >= 0) Q_EMIT dataChanged(index(before), index(before), {Selected});
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row), {Selected});
}

// ---- ColumnsModel -------------------------------------------------------------------

QVariant ColumnsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Slot& s = m_columns[static_cast<std::size_t>(index.row())];
    const Column& c = s.column;
    switch (role) {
    case Title: return c.title;
    case Current: return c.plan.current;
    case Peek: return c.plan.peek;
    case Crumb: return static_cast<int>(c.plan.crumb);
    case SelectedRow: return c.selectedRow;
    case Entries: return QVariant::fromValue(s.entries);
    case EntryCount: return s.entries->rowCount();
    case Error: return c.error;
    }
    return {};
}

QHash<int, QByteArray> ColumnsModel::roleNames() const {
    return {{Title, "title"}, {Current, "current"}, {Peek, "peek"}, {Crumb, "crumb"}, {SelectedRow, "selectedRow"},
            {Entries, "entries"}, {EntryCount, "entryCount"}, {Error, "error"}};
}

QVariantMap ColumnsModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

std::vector<core::ColumnPlan> ColumnsModel::plans() const {
    std::vector<core::ColumnPlan> out;
    out.reserve(m_columns.size());
    for (const Slot& s : m_columns) out.push_back(s.column.plan);
    return out;
}

void ColumnsModel::truncate(std::size_t n) {
    if (n >= m_columns.size()) return;
    beginRemoveRows({}, static_cast<int>(n), count() - 1);
    // Later: a delegate on its way out may still hold its column's model.
    for (std::size_t i = n; i < m_columns.size(); ++i) m_columns[i].entries->deleteLater();
    m_columns.resize(n);
    endRemoveRows();
    Q_EMIT countChanged();
}

void ColumnsModel::append(Column column) {
    auto* entries = new EntriesModel(this);
    QQmlEngine::setObjectOwnership(entries, QQmlEngine::CppOwnership);
    entries->set(column.listing);
    entries->setSelected(column.selectedRow);
    beginInsertRows({}, count(), count());
    m_columns.push_back({std::move(column), entries});
    endInsertRows();
    Q_EMIT countChanged();
}

void ColumnsModel::update(std::size_t i, Column column) {
    Slot& s = m_columns[i];
    const Column& was = s.column;
    QList<int> roles;
    if (column.title != was.title) roles << Title;
    if (column.plan.current != was.plan.current) roles << Current;
    if (column.plan.peek != was.plan.peek) roles << Peek;
    if (column.plan.crumb != was.plan.crumb) roles << Crumb;
    if (column.error != was.error) roles << Error;
    if (column.listing != was.listing) {
        s.entries->set(column.listing);
        s.entries->setSelected(column.selectedRow);
        roles << EntryCount << SelectedRow;
    } else if (column.selectedRow != was.selectedRow) {
        s.entries->setSelected(column.selectedRow);
        roles << SelectedRow;
    }
    s.column = std::move(column);
    if (!roles.isEmpty()) Q_EMIT dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), roles);
}

void ColumnsModel::clear() { truncate(0); }

// ---- DirTreeModel -------------------------------------------------------------------

void DirTreeModel::reset(core::Browser* browser) {
    beginResetModel();
    m_browser = browser;
    m_root.reset();
    if (browser) {
        m_root = std::make_unique<Node>();
        m_root->name = "/";
        m_root->path = "/";
        m_root->inode = browser->breadcrumbs().front().inode;
        load(m_root.get());
    }
    endResetModel();
}

void DirTreeModel::setCurrentPath(const QString& path) {
    if (path == m_currentPath) return;
    m_currentPath = path;
    if (!m_root) return;
    Q_EMIT dataChanged(index(0, 0), index(0, 0), {IsCurrent});
    std::vector<Node*> stack{m_root.get()};
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) {
            const QModelIndex i = indexOf(c.get());
            Q_EMIT dataChanged(i, i, {IsCurrent});
            stack.push_back(c.get());
        }
    }
}

void DirTreeModel::load(Node* n) {
    if (!n || n->loaded || !m_browser) return;
    n->loaded = true;
    auto listing = m_browser->list(n->inode);
    if (!listing) {
        n->hasDirs = false;
        return;
    }
    for (const auto& e : listing->entries) {
        if (!e.isDir()) continue;
        auto child = std::make_unique<Node>();
        child->name = qs(e.entry.name);
        child->path = n->path == "/" ? "/" + child->name : n->path + "/" + child->name;
        child->inode = e.entry.inode;
        child->parent = n;
        child->row = static_cast<int>(n->children.size());
        n->children.push_back(std::move(child));
    }
    n->hasDirs = !n->children.empty();
}

QModelIndex DirTreeModel::indexOf(Node* n) const {
    if (!n) return {};
    if (n == m_root.get()) return createIndex(0, 0, n);
    return createIndex(n->row, 0, n);
}

QModelIndex DirTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column != 0 || row < 0 || !m_root) return {};
    if (!parent.isValid()) return row == 0 ? createIndex(0, 0, m_root.get()) : QModelIndex();
    Node* p = nodeOf(parent);
    if (!p || static_cast<std::size_t>(row) >= p->children.size()) return {};
    return createIndex(row, 0, p->children[static_cast<std::size_t>(row)].get());
}

QModelIndex DirTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    Node* n = nodeOf(child);
    if (!n || !n->parent) return {};
    return indexOf(n->parent);
}

int DirTreeModel::rowCount(const QModelIndex& parent) const {
    if (!m_root) return 0;
    if (!parent.isValid()) return 1;
    Node* n = nodeOf(parent);
    if (!n) return 0;
    if (!n->loaded) const_cast<DirTreeModel*>(this)->load(n);
    return static_cast<int>(n->children.size());
}

bool DirTreeModel::hasChildren(const QModelIndex& parent) const {
    if (!parent.isValid()) return m_root != nullptr;
    Node* n = nodeOf(parent);
    return n && (n->loaded ? !n->children.empty() : n->hasDirs);
}

bool DirTreeModel::canFetchMore(const QModelIndex& parent) const {
    if (!parent.isValid()) return false;
    Node* n = nodeOf(parent);
    return n && !n->loaded;
}

void DirTreeModel::fetchMore(const QModelIndex& parent) {
    Node* n = nodeOf(parent);
    if (!n || n->loaded) return;
    load(n);
    if (!n->children.empty()) {
        beginInsertRows(parent, 0, static_cast<int>(n->children.size()) - 1);
        endInsertRows();
    }
}

QVariant DirTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    Node* n = nodeOf(index);
    if (!n) return {};
    switch (role) {
    case Qt::DisplayRole:
    case Name: return n->name;
    case Path: return n->path;
    case IsCurrent: return n->path == m_currentPath;
    }
    return {};
}

QHash<int, QByteArray> DirTreeModel::roleNames() const { return {{Name, "name"}, {Path, "path"}, {IsCurrent, "isCurrent"}, {Qt::DisplayRole, "display"}}; }

QModelIndex DirTreeModel::indexForPath(const QString& path) {
    if (!m_root) return {};
    Node* n = m_root.get();
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        load(n);
        Node* next = nullptr;
        for (auto& c : n->children)
            if (c->name == part) next = c.get();
        if (!next) return indexOf(n);
        n = next;
    }
    return indexOf(n);
}

// ---- FileBrowser -----------------------------------------------------------------------

FileBrowser::FileBrowser(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        sendRequest();
        Q_EMIT selectionChanged();
    });
    m_grace.setSingleShot(true);
    m_grace.setInterval(kGraceMs);
    connect(&m_grace, &QTimer::timeout, this, &FileBrowser::graceEnded);
    m_stall.setSingleShot(true);
    m_stall.setInterval(kStallMs);
    connect(&m_stall, &QTimer::timeout, this, &FileBrowser::requestStalled);
    m_peekTimer.setSingleShot(true);
    m_peekTimer.setInterval(kDebounceMs);
    connect(&m_peekTimer, &QTimer::timeout, this, [this] {
        m_peekRow = m_entries.selected();
        syncColumns();
    });
    connect(Settings::instance(), &Settings::previewFilesChanged, this, &FileBrowser::previewSettingChanged);
    connect(Settings::instance(), &Settings::browseLayoutChanged, this, &FileBrowser::layoutChanged);
    connect(Workspace::instance(), &Workspace::uiLockedChanged, this, &FileBrowser::uiLockChanged);
    connect(Workspace::instance(), &Workspace::selectionChanged, this, [this] {
        if (Workspace::instance()->selectedPath() != m_path || !m_browser) reload();
    });
    connect(Workspace::instance(), &Workspace::currentChanged, this, &FileBrowser::reload);
    connect(Workspace::instance(), &Workspace::viewChanged, this, [this] {
        if (Workspace::instance()->view() == "browse" && !m_browser) reload();
    });
    connect(JobRunner::instance(), &JobRunner::runningChanged, this, [this] {
        if (!JobRunner::instance()->running()) setBusy(false);
    });
    reload();
}

FileBrowser::~FileBrowser() { m_loader.reset(); }

void FileBrowser::setBusy(bool b) {
    if (b == m_busy) return;
    m_busy = b;
    Q_EMIT changed();
    if (!m_busy && m_reopen) {
        const auto [volume, snapshot] = *std::exchange(m_reopen, std::nullopt);
        openSubvolume(volume, snapshot);
    }
    if (!m_busy && m_columnsStale) syncColumns();
}

void FileBrowser::reload() { openSubvolume({}, {}); }

void FileBrowser::openSubvolume(const QString& volume, const QString& snapshot) {
    if (m_busy) {
        // One of this object's jobs is reading through m_browser on the job thread.
        m_reopen.emplace(volume, snapshot);
        return;
    }
    m_reopen.reset();
    dropLoader();
    // Inode numbers mean nothing across browsers: no column and no listing outlives this one.
    m_peekTimer.stop();
    m_peekRow = -1;
    m_columnsStale = false;
    m_columns.clear();
    m_listings.clear();
    m_browser.reset();
    ++m_generation;
    m_reason.clear();
    m_subvolume = volume;
    m_snapshot = snapshot;
    Workspace* w = Workspace::instance();
    m_path = w->selectedPath();
    if (!w->tree()) m_reason = "Open a disk or an image, then pick a filesystem.";
    else if (w->selectedMetadataIndex() >= 0) m_reason = "Metadata regions hold no files.";
    else {
        // The device node or a bare partition holds no files: fall through to the first readable node.
        const auto* node = core::nodeAt(*w->tree(), m_path);
        const bool readable = node && node->content && stein::fs::has(node->content->capabilities(), stein::fs::Capability::Read);
        if (!readable && w->view() == "browse") {
            for (const auto& r : core::topologyRows(*w->tree(), false))
                if (r.canBrowse) {
                    m_path = r.path;
                    w->selectPath(pathToVariant(m_path));
                    break;
                }
        }
        stein::fs::ReaderOptions o;
        o.volume = ss(volume);
        o.snapshot = ss(snapshot);
        auto b = core::Browser::open(*w->tree(), m_path, o);
        if (b) m_browser.emplace(std::move(*b));
        else m_reason = qs(core::present(b.error()).message);
    }
    m_tree.reset(m_browser ? &*m_browser : nullptr);
    listCurrent();
    Q_EMIT changed();
}

void FileBrowser::listCurrent(const std::string& reselect, bool preview) {
    m_currentError.clear();
    if (!m_browser) {
        m_entries.set(nullptr);
    } else if (auto listing = readCurrent(); !listing) {
        Workspace::instance()->reportError(listing.error());
        m_currentError = qs(core::present(listing.error()).message);
        m_entries.set(nullptr);
    } else {
        m_entries.set(std::move(*listing));
        if (!reselect.empty()) {
            const auto& rows = m_entries.entries();
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const core::Entry& e) { return e.isDir() && e.entry.name == reselect; });
            if (it != rows.end()) m_entries.setSelected(static_cast<int>(it - rows.begin()));
        }
    }
    if (m_browser) m_tree.setCurrentPath(pathText());
    m_hashText = "not computed";
    if (preview) refreshPreview(PreviewStart::Now);
    // A step to another directory shows the folder it left (up, a jump) at once.
    m_peekTimer.stop();
    m_peekRow = m_entries.selected();
    syncColumns();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
}

// The column layout lists through the cache, which then holds the listing the next step
// reuses: the peek that is entered, the parent that is gone up to.
stein::Expected<std::shared_ptr<const core::Listing>> FileBrowser::readCurrent() {
    if (columnLayout()) return m_listings.get(*m_browser, m_browser->current());
    auto listing = m_browser->listCurrent();
    if (!listing) return stein::fail(listing.error());
    return std::make_shared<const core::Listing>(std::move(*listing));
}

QString FileBrowser::title() const { return m_browser ? qs(m_browser->title()) : QString(); }
QString FileBrowser::fsName() const { return m_browser ? qs(m_browser->fsName()) : QString(); }
QString FileBrowser::pathText() const { return m_browser ? qs(m_browser->pathText()) : QString(); }

QVariantList FileBrowser::breadcrumbs() const {
    QVariantList out;
    if (!m_browser) return out;
    const auto& crumbs = m_browser->breadcrumbs();
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        QVariantMap m;
        m["name"] = qs(crumbs[i].name);
        m["index"] = static_cast<int>(i);
        out.push_back(m);
    }
    return out;
}

QString FileBrowser::statusText() const {
    if (!m_browser) return {};
    const int n = m_entries.rowCount();
    QString s = QString::number(n) + (n == 1 ? " entry" : " entries");
    if (m_entries.selected() >= 0) s += " · 1 selected";
    return s;
}

QVariantList FileBrowser::subvolumes() const {
    QVariantList out;
    const Workspace* w = Workspace::instance();
    if (!w->tree()) return out;
    const auto* node = core::nodeAt(*w->tree(), m_path);
    if (!node || !node->content) return out;
    for (const auto& s : node->content->subvolumes()) {
        QVariantMap m;
        m["kind"] = qs(s.kind);
        m["name"] = qs(s.name);
        m["parent"] = qs(s.parent);
        m["note"] = qs(s.note);
        out.push_back(m);
    }
    return out;
}

bool FileBrowser::canMount() const { return m_browser && core::MountManager::available(); }

const core::Entry* FileBrowser::selectedEntry() const {
    const int i = m_entries.selected();
    return i < 0 ? nullptr : &m_entries.entries()[static_cast<std::size_t>(i)];
}

QString FileBrowser::selectedName() const {
    const int i = m_entries.selected();
    return i < 0 ? QString() : qs(m_entries.entries()[static_cast<std::size_t>(i)].entry.name);
}

QString FileBrowser::selectedInfo() const {
    const int i = m_entries.selected();
    if (i < 0) return {};
    const auto& e = m_entries.entries()[static_cast<std::size_t>(i)];
    QString s = "inode " + QString::number(e.entry.inode.id) + " · " + qs(core::sizeBinary(e.stat.size)) + " · " + qs(e.modeText);
    s += " " + QString::number(e.stat.uid) + ":" + QString::number(e.stat.gid);
    if (e.stat.nlink > 1) s += " · " + QString::number(e.stat.nlink) + " links";
    return s;
}

QString FileBrowser::mtimeText() const {
    const int i = m_entries.selected();
    return i < 0 ? QString() : qs(m_entries.entries()[static_cast<std::size_t>(i)].mtimeText);
}

QString FileBrowser::crtimeText() const {
    const int i = m_entries.selected();
    if (i < 0) return {};
    const auto t = m_entries.entries()[static_cast<std::size_t>(i)].stat.crtime;
    return t ? qs(core::timeText(t)) : "—";
}

void FileBrowser::setShowHex(bool on) {
    if (on == m_showHex) return;
    m_showHex = on;
    Q_EMIT selectionChanged();
}

// ---- Preview: the GUI thread's half ---------------------------------------------------------

bool FileBrowser::previewEnabled() const { return Settings::instance()->previewFiles(); }

// A job that holds the UI may be writing to the disk under the reader; and while a reload waits
// for this object's own job, m_path no longer names the browsed node in the workspace's tree.
bool FileBrowser::previewHeld() const { return Workspace::instance()->uiLocked() || m_reopen.has_value(); }

void FileBrowser::refreshPreview(PreviewStart start) {
    const core::Entry* e = selectedEntry();
    if (!m_browser || !e) return settlePreview({});
    if (e->stat.type != stein::fs::FileType::File || e->stat.size == 0) {
        // Nothing to read: core answers these before it touches the reader.
        PreviewContent described;
        described.kind = "empty";
        if (auto pv = core::preview(m_browser->reader(), e->entry.inode, e->stat)) described.note = qs(pv->note);
        if (!e->linkTarget.empty()) described.note = "→ " + qs(e->linkTarget);
        return settlePreview(described);
    }
    const PreviewKey key{e->entry.inode.id, e->stat.size, e->stat.mtime};
    if (m_previewLoading && m_request && m_request->key == key) {
        // Already wanted. A click does not wait for the keyboard's pause.
        if (start != PreviewStart::Debounced && m_debounce.isActive()) {
            m_debounce.stop();
            sendRequest();
        }
        return;
    }
    if (start != PreviewStart::Forced && !previewEnabled()) {
        // A file previewed once with previews off leaves no reader behind once the selection moves on.
        if (m_loader) dropLoader();
        PreviewContent off;
        off.kind = "off";
        return settlePreview(off);
    }
    if (const auto known = cachedPreview(key)) return settlePreview(*known);

    m_debounce.stop();
    cancelTicket();
    if (std::exchange(m_previewHeld, false)) showLoading();
    m_request = PreviewRequest{key, core::PreviewTarget{m_browser->pathTo(e->entry.name), e->stat}};
    if (!m_previewLoading) {
        m_previewLoading = true;
        m_grace.start();
    }
    if (start == PreviewStart::Debounced) m_debounce.start();
    else sendRequest();
}

void FileBrowser::settlePreview(const PreviewContent& content) {
    m_debounce.stop();
    m_grace.stop();
    cancelTicket();
    m_request.reset();
    m_previewLoading = false;
    m_previewHeld = false;
    m_previewKind = content.kind;
    m_previewText = content.text;
    m_previewHex = content.hex;
    m_previewImage = content.image;
    m_previewNote = content.note;
}

void FileBrowser::showLoading(const QString& note) {
    m_grace.stop();
    m_previewKind = "loading";
    m_previewText.clear();
    m_previewHex.clear();
    m_previewImage = {};
    m_previewNote = note;
}

void FileBrowser::graceEnded() {
    if (!m_previewLoading) return;
    showLoading();
    Q_EMIT selectionChanged();
}

void FileBrowser::cancelTicket() {
    m_stall.stop();
    if (!m_ticket) return;
    if (m_loader) m_loader->cancel();
    m_ticket = 0;
}

void FileBrowser::sendRequest() {
    if (!m_request) return;
    if (previewHeld()) {
        m_previewHeld = true;
        showLoading("waiting for the running job");
        return;
    }
    if (std::exchange(m_previewHeld, false)) showLoading();
    if (!ensureLoader()) {
        PreviewContent failed;
        failed.kind = "unreadable";
        failed.note = m_loaderError;
        return settlePreview(failed);
    }
    cancelTicket();
    m_ticket = m_loader->request(m_request->target);
    m_stall.start();
}

bool FileBrowser::ensureLoader() {
    if (m_loader) return true;
    if (!m_loaderError.isEmpty()) return false;
    const Workspace* w = Workspace::instance();
    if (!m_browser || !w->tree()) {
        m_loaderError = "the source is no longer open";
        return false;
    }
    // The same node and options m_browser was opened with (openSubvolume).
    stein::fs::ReaderOptions o;
    o.volume = ss(m_subvolume);
    o.snapshot = ss(m_snapshot);
    auto reader = core::openReader(*w->tree(), m_path, o);
    if (!reader) {
        m_loaderError = qs(core::present(reader.error()).message);
        return false;
    }
    const quint64 epoch = ++m_epoch;
    auto deliver = [this, epoch](core::PreviewResult result) {
        // On the worker, under the loader's lock: `this` is only the address the result is posted
        // to, which is safe because nothing is delivered once ~PreviewLoader has returned. The
        // file's bytes end here; what is shown crosses over.
        const quint64 ticket = result.ticket;
        auto content = result.preview ? std::static_pointer_cast<const PreviewContent>(result.decoded) : nullptr;
        QString failure = result.preview ? QString() : qs(result.preview.error().message());
        result = {};
        QMetaObject::invokeMethod(this, [this, epoch, ticket, content = std::move(content), failure = std::move(failure)] { previewArrived(epoch, ticket, content, failure); },
                                  Qt::QueuedConnection);
    };
    try {
        m_loader = std::make_unique<core::PreviewLoader>(std::move(*reader), core::PreviewLimits{}, &renderPreview, std::move(deliver));
    } catch (const std::exception& e) {
        // No thread to be had: as if the source could not be read, and not retried per file.
        m_loaderError = QString::fromLocal8Bit(e.what());
        return false;
    }
    return true;
}

void FileBrowser::dropLoader() {
    m_debounce.stop();
    m_grace.stop();
    m_stall.stop();
    m_loader.reset();
    ++m_epoch;
    m_ticket = 0;
    m_request.reset();
    m_previewLoading = false;
    m_previewHeld = false;
    m_loaderError.clear();
    m_cache.clear();
}

void FileBrowser::previewArrived(quint64 epoch, quint64 ticket, std::shared_ptr<const PreviewContent> content, const QString& failure) {
    if (epoch != m_epoch || ticket != m_ticket) return;
    m_ticket = 0;
    if (!content) {
        auto failed = std::make_shared<PreviewContent>();
        failed->kind = "unreadable";
        failed->note = failure.isEmpty() ? QString("the preview could not be made") : failure;
        content = std::move(failed);
    } else if (content->kind != "unreadable" && content->remember && m_request) {
        // A failed read is not remembered: coming back to the file tries again. Nor is an SVG
        // that waited in vain for earlier ones to finish drawing. One that took too long to draw
        // is, so that coming back does not draw it again.
        rememberPreview(m_request->key, content);
    }
    settlePreview(*content);
    Q_EMIT selectionChanged();
}

void FileBrowser::requestStalled() {
    if (!m_ticket || !m_loader) return;
    // Only a decode of a file given up for this one is left behind. A read that hangs would hold
    // up a new reader on the same disk too (opened right here, on this thread), and this
    // request's own decode would only start over.
    const core::PreviewActivity busy = m_loader->activity();
    if (busy.phase != core::PreviewActivity::Phase::Transforming || busy.ticket == m_ticket) return;
    if (core::PreviewLoader::workersAlive() >= kMaxPreviewWorkers) return;
    // The old worker is left to finish what it is in; it owns its reader and delivers nothing more.
    m_loader.reset();
    ++m_epoch;
    m_ticket = 0;
    sendRequest();
    Q_EMIT selectionChanged();
}

void FileBrowser::previewSettingChanged() {
    // Off: the second reader goes too, so nothing of this app reads file content behind the user's back.
    if (!previewEnabled()) dropLoader();
    refreshPreview(PreviewStart::Now);
    // On: "off" is no previous preview to keep up while the file is read.
    if (m_previewLoading && m_previewKind == "off") showLoading();
    Q_EMIT selectionChanged();
}

void FileBrowser::uiLockChanged() {
    if (Workspace::instance()->uiLocked()) {
        // The job may write under the reader: what is being read now waits for it like the rest.
        if (!m_ticket && !m_debounce.isActive()) return;
        m_debounce.stop();
        cancelTicket();
        m_previewHeld = true;
        showLoading("waiting for the running job");
        Q_EMIT selectionChanged();
        return;
    }
    if (!m_previewHeld || previewHeld()) return;
    sendRequest();
    Q_EMIT selectionChanged();
}

void FileBrowser::previewOnce() {
    const core::Entry* e = selectedEntry();
    if (!m_browser || !e || m_busy || previewHeld()) return;
    refreshPreview(PreviewStart::Forced);
    Q_EMIT selectionChanged();
}

std::shared_ptr<const PreviewContent> FileBrowser::cachedPreview(const PreviewKey& key) {
    const auto it = std::find_if(m_cache.begin(), m_cache.end(), [&](const CachedPreview& c) { return c.key == key; });
    if (it == m_cache.end()) return nullptr;
    std::rotate(m_cache.begin(), it, it + 1);
    return m_cache.front().content;
}

void FileBrowser::rememberPreview(const PreviewKey& key, std::shared_ptr<const PreviewContent> content) {
    std::erase_if(m_cache, [&](const CachedPreview& c) { return c.key == key; });
    m_cache.push_front(CachedPreview{key, std::move(content)});
    qsizetype bytes = 0;
    auto end = m_cache.begin();
    for (std::size_t n = 0; end != m_cache.end() && n < kCacheEntries; ++end, ++n)
        if ((bytes += costOf(*end->content)) > kCacheBytes) break;
    m_cache.erase(end, m_cache.end());
}

// ---- Selection and navigation -------------------------------------------------------------

void FileBrowser::activate(int row) {
    if (!m_browser || row < 0 || row >= m_entries.rowCount() || m_busy) return;
    const auto& e = m_entries.entries()[static_cast<std::size_t>(row)];
    if (!e.isDir()) return select(row);
    const stein::fs::DirEntry picked = e.entry;   // the listing it is in goes when the folder opens
    enterDirectory(picked.name, &picked.inode);
}

void FileBrowser::selectRow(int row, PreviewStart start, bool previewStale) {
    const int before = m_entries.selected();
    m_entries.setSelected(row);
    const bool moved = m_entries.selected() != before;
    if (moved) m_hashText = "not computed";
    // A click on the row that is selected already changes nothing that is shown, except that it
    // hurries a preview along and tries an unreadable file once more.
    if (moved || previewStale || m_previewLoading || m_previewKind == "unreadable") refreshPreview(start);
    if (columnLayout()) {
        // The peek, which may read a directory, waits for the keyboard's pause like the preview.
        if (start == PreviewStart::Debounced) {
            if (moved) m_peekTimer.start();
        } else {
            m_peekTimer.stop();
            m_peekRow = m_entries.selected();
        }
        // A click also tries a column that could not be read once more; a key step does not.
        syncColumns(start == PreviewStart::Now);
    }
    Q_EMIT selectionChanged();
}

void FileBrowser::select(int row) {
    if (m_busy) return;
    selectRow(row, PreviewStart::Now);
}

void FileBrowser::moveSelection(int delta) {
    if (!m_browser || m_busy) return;
    const int row = core::movedIndex(m_entries.selected(), m_entries.rowCount(), delta);
    if (row >= 0 && row != m_entries.selected()) selectRow(row, PreviewStart::Debounced);
}

void FileBrowser::selectFirst() {
    if (!m_browser || m_busy || m_entries.rowCount() == 0 || m_entries.selected() == 0) return;
    selectRow(0, PreviewStart::Debounced);
}

void FileBrowser::selectLast() {
    const int last = m_entries.rowCount() - 1;
    if (!m_browser || m_busy || last < 0 || m_entries.selected() == last) return;
    selectRow(last, PreviewStart::Debounced);
}

void FileBrowser::activateSelected() {
    const core::Entry* e = selectedEntry();
    if (!m_browser || m_busy || !e || !e->isDir()) return;
    const stein::fs::DirEntry picked = e->entry;
    enterDirectory(picked.name, &picked.inode);
}

void FileBrowser::enter(const QString& name) {
    if (!m_browser || m_busy) return;
    enterDirectory(ss(name));
}

bool FileBrowser::enterDirectory(const std::string& name, const stein::fs::Inode* listed, bool preview) {
    if (auto r = listed ? m_browser->enter(name, *listed) : m_browser->enter(name); !r) {
        Workspace::instance()->reportError(r.error());
        return false;
    }
    listCurrent({}, preview);
    return true;
}

void FileBrowser::enterSelectedAndSelectFirst() {
    const core::Entry* e = selectedEntry();
    if (!m_browser || m_busy || !e || !e->isDir()) return;
    if (const stein::fs::DirEntry picked = e->entry; !enterDirectory(picked.name, &picked.inode)) return;
    if (m_entries.rowCount() > 0) selectRow(0, PreviewStart::Debounced);
}

void FileBrowser::up() {
    if (!m_browser || m_busy) return;
    if (const std::string left = m_browser->up(); !left.empty()) listCurrent(left);
}

void FileBrowser::jumpTo(int crumb) {
    if (!m_browser || m_busy) return;
    if (const std::string left = m_browser->jumpTo(static_cast<std::size_t>(std::max(0, crumb))); !left.empty()) listCurrent(left);
}

void FileBrowser::goTo(const QString& path) {
    if (!m_browser || m_busy) return;
    if (auto r = m_browser->goTo(ss(path)); !r) {
        Workspace::instance()->reportError(r.error());
        return;
    }
    listCurrent();
}

// ---- The column layout --------------------------------------------------------------------

bool FileBrowser::columnLayout() const { return Settings::instance()->browseColumns(); }

void FileBrowser::layoutChanged() {
    m_peekTimer.stop();
    if (!columnLayout()) {
        m_columns.clear();
        m_listings.clear();
        m_peekRow = -1;
        m_columnsStale = false;
        return;
    }
    m_listings.put(m_entries.listing());
    m_peekRow = m_entries.selected();
    syncColumns();
}

void FileBrowser::syncColumns(bool retryFailed) {
    if (!columnLayout() || !m_browser) {
        m_columns.clear();
        return;
    }
    if (m_busy) {
        m_columnsStale = true;
        return;
    }
    m_columnsStale = false;
    const auto& rows = m_entries.entries();
    const core::Entry* peek = m_peekRow >= 0 && static_cast<std::size_t>(m_peekRow) < rows.size() ? &rows[static_cast<std::size_t>(m_peekRow)] : nullptr;
    const auto plans = core::planColumns(m_browser->breadcrumbs(), peek);
    const std::size_t kept = core::keptColumns(m_columns.plans(), plans);
    m_columns.truncate(kept);
    for (std::size_t i = 0; i < plans.size(); ++i) {
        if (i < kept) m_columns.update(i, columnFor(plans[i], &m_columns.at(i), retryFailed));
        else m_columns.append(columnFor(plans[i], nullptr, retryFailed));
    }
}

// `before`: what the column showed when it listed the same directory. `retryFailed`: if that
// listing failed, read the directory again (the cache keeps no failure).
ColumnsModel::Column FileBrowser::columnFor(const core::ColumnPlan& plan, const ColumnsModel::Column* before, bool retryFailed) {
    ColumnsModel::Column c;
    c.plan = plan;
    c.title = plan.crumb == 0 ? title() : qs(plan.title);
    if (plan.current) {
        c.listing = m_entries.listing();
        c.error = m_currentError;
        c.selectedRow = m_entries.selected();
    } else {
        if (before && (before->listing || !retryFailed)) {
            c.listing = before->listing;
            c.error = before->error;
        } else if (auto listing = m_listings.get(*m_browser, plan.dir)) {
            c.listing = std::move(*listing);
        } else {
            c.error = qs(core::present(listing.error()).message);
        }
        // The row it showed holds only for the very listing it showed it in: not after a retry.
        const bool sameRow = before && !before->plan.current && before->listing == c.listing && before->plan.selectName == plan.selectName;
        if (plan.peek || !c.listing) c.selectedRow = -1;
        else c.selectedRow = sameRow ? before->selectedRow : core::rowOfName(*c.listing, plan.selectName, m_browser->caseSensitive());
    }
    // What is shown stays cached, so the step back to it finds the very listing its column holds.
    if (c.listing && !m_listings.find(plan.dir)) m_listings.put(c.listing);
    return c;
}

bool FileBrowser::clickColumn(int column, int row) {
    if (!m_browser || m_busy || column < 0) return false;
    const auto plans = m_columns.plans();
    const auto at = static_cast<std::size_t>(column);
    // A step to another directory leaves the preview alone until the clicked row is selected:
    // the folder passed on the way is never shown under the clicked file's name, and the last
    // file's preview stays up for the moment the next one takes, as after a click in the list.
    bool stepped = false;
    switch (core::classifyClick(at, plans)) {
    case core::ColumnClick::Ignore: return false;
    case core::ColumnClick::SelectInCurrent: break;
    case core::ColumnClick::JumpThenSelect: {
        const std::string left = m_browser->jumpTo(plans[at].crumb);
        if (left.empty()) return false;
        listCurrent(left, false);
        stepped = true;
        break;
    }
    case core::ColumnClick::EnterThenSelect:
        // The folder the peek shows: within the keyboard's pause the selection may have moved on.
        if (!enterDirectory(plans[at].title, &plans[at].dir, false)) return false;
        stepped = true;
        break;
    }
    if (row >= 0 && row < m_entries.rowCount()) {
        selectRow(row, PreviewStart::Now, stepped);
    } else if (stepped) {
        refreshPreview(PreviewStart::Now);
        Q_EMIT selectionChanged();
    }
    return true;
}

void FileBrowser::columnClicked(int column, int row) { clickColumn(column, row); }

void FileBrowser::columnActivated(int column, int row) {
    if (!clickColumn(column, row) || row < 0 || m_entries.selected() != row) return;
    activateSelected();
}

void FileBrowser::hash() {
    const int i = m_entries.selected();
    if (!m_browser || i < 0 || m_busy) return;
    const auto e = m_entries.entries()[static_cast<std::size_t>(i)];
    if (e.stat.type != stein::fs::FileType::File) return;
    core::Browser* browser = &*m_browser;
    auto out = std::make_shared<std::string>();
    const stein::fs::DirEntry hashed = e.entry;
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Hashing " + qs(e.entry.name), "browse",
        [browser, e, out](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            report.start();
            auto h = core::sha256Hex(browser->reader(), e.entry.inode, e.stat, progress);
            if (!h) {
                report.finish(stein::ReportStatus::Error);
                return stein::fail(h.error());
            }
            *out = *h;
            report.addDetail("sha256", *h);
            report.finish(stein::ReportStatus::Success);
            return {};
        },
        [this, out, hashed](bool ok, const stein::Error&) {
            // The hash belongs to the entry, not to the row it was on when the job began. Before
            // setBusy: that may run a reload that waited for this job.
            const core::Entry* now = selectedEntry();
            if (ok && now && now->entry.inode == hashed.inode && now->entry.name == hashed.name) {
                m_hashText = qs(*out);
                Q_EMIT selectionChanged();
            }
            setBusy(false);
        });
    if (!started) setBusy(false);
}

std::optional<FileBrowser::CopyTarget> FileBrowser::copyTarget(int row) const {
    if (!m_browser) return std::nullopt;
    if (row < 0) {
        const std::string name = m_browser->breadcrumbs().size() > 1 ? m_browser->breadcrumbs().back().name : ss(title());
        return CopyTarget{m_generation, m_browser->current(), name};
    }
    if (row >= m_entries.rowCount()) return std::nullopt;
    const auto& e = m_entries.entries()[static_cast<std::size_t>(row)];
    return CopyTarget{m_generation, e.entry.inode, e.entry.name};
}

void FileBrowser::copyOut(int row, const QUrl& directory) {
    if (row < 0 || m_busy) return;
    if (const auto target = copyTarget(row)) startCopy(target->inode, target->name, directory);
}

void FileBrowser::copyAll(const QUrl& directory) {
    if (m_busy) return;
    if (const auto target = copyTarget(-1)) startCopy(target->inode, target->name, directory);
}

void FileBrowser::prepareCopy(int row) { m_copyTarget = copyTarget(row); }

void FileBrowser::copyPrepared(const QUrl& directory) {
    if (!m_copyTarget || m_busy) return;
    const CopyTarget target = *std::exchange(m_copyTarget, std::nullopt);
    if (!m_browser || target.generation != m_generation) {
        QVariantMap e;
        e["title"] = "Nothing copied";
        e["message"] = "The disk was read again while the folder was being chosen: “" + qs(target.name) + "” may not be what it was.";
        e["hint"] = "Select it again, then Copy out.";
        e["severity"] = "warning";
        Q_EMIT Workspace::instance()->error(e);
        return;
    }
    startCopy(target.inode, target.name, directory);
}

void FileBrowser::startCopy(const stein::fs::Inode& source, const std::string& name, const QUrl& directory) {
    if (!m_browser || m_busy) return;
    core::Browser* browser = &*m_browser;
    const std::filesystem::path dest = pathOf(directory);
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Copying out " + qs(name), "browse",
        [browser, source, name, dest](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            report.start();
            report.addDetail("destination", (dest / name).string());
            auto stats = core::copyOut(browser->reader(), source, name, dest, progress);
            if (!stats) {
                report.addLine(stats.error().toString());
                report.finish(stein::ReportStatus::Error);
                return stein::fail(stats.error());
            }
            report.addDetail("copied", core::copyStatsText(*stats));
            for (const auto& w : stats->warnings) report.addLine("skipped " + w);
            report.finish(stats->warnings.empty() ? stein::ReportStatus::Success : stein::ReportStatus::Warning);
            JobRunner::instance()->setSummary(qs(core::copyStatsText(*stats)));
            return {};
        },
        [this](bool, const stein::Error&) { setBusy(false); });
    if (!started) setBusy(false);
}

namespace {
QString stagingRoot() { return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/drstein-drag-" + QString::number(QCoreApplication::applicationPid()); }
int g_stagingCounter = 0;
} // namespace

QStringList FileBrowser::stageForDrag(const QList<int>& rows) {
    if (!m_browser || m_busy) return {};
    std::vector<core::Entry> picked;
    for (int row : rows)
        if (row >= 0 && row < m_entries.rowCount()) picked.push_back(m_entries.entries()[static_cast<std::size_t>(row)]);
    return stage(picked);
}

QStringList FileBrowser::stageColumnForDrag(int column, int row) {
    if (!m_browser || m_busy || column < 0 || column >= m_columns.count()) return {};
    const auto& listing = m_columns.at(static_cast<std::size_t>(column)).listing;
    if (!listing || row < 0 || static_cast<std::size_t>(row) >= listing->entries.size()) return {};
    return stage({listing->entries[static_cast<std::size_t>(row)]});
}

QStringList FileBrowser::stage(const std::vector<core::Entry>& picked) {
    QStringList urls;
    if (picked.empty()) return urls;
    quint64 total = 0;
    for (const auto& e : picked)
        if (e.stat.type == stein::fs::FileType::File) total += e.stat.size;
    if (total > kDragLimit) {
        Workspace::instance()->reportError(stein::Error(stein::ErrorCategory::InvalidArgument, "that is " + core::sizeText(total) + "; drag-out stages a copy first and stops at " + core::sizeText(kDragLimit) + ". Use Copy out\u2026 for this one."));
        return urls;
    }
    const QString dir = stagingRoot() + "/" + QString::number(++g_stagingCounter);
    if (!QDir().mkpath(dir)) {
        Workspace::instance()->reportError(stein::Error(stein::ErrorCategory::Io, "could not create the staging folder " + ss(dir)));
        return urls;
    }
    stein::NullProgressSink sink;
    stein::Progress progress(sink);
    for (const auto& e : picked) {
        auto stats = core::copyOut(m_browser->reader(), e.entry.inode, e.entry.name, ss(dir), progress);
        if (!stats) {
            Workspace::instance()->reportError(stats.error());
            continue;
        }
        urls << QUrl::fromLocalFile(dir + "/" + qs(e.entry.name)).toString();
    }
    return urls;
}

void FileBrowser::cleanupStaging() {
    QDir(stagingRoot()).removeRecursively();
}

void FileBrowser::mount() {
    Workspace* w = Workspace::instance();
    // While a reload waits for this object's job, m_path may name another node of a newer tree.
    if (!m_browser || !w->tree() || !w->current() || m_reopen) return;
    // A mount needs its own reader: the mount thread owns it.
    stein::fs::ReaderOptions o;
    o.volume = ss(m_subvolume);
    o.snapshot = ss(m_snapshot);
    const auto* node = core::nodeAt(*w->tree(), m_path);
    if (!node || !node->content) return;
    auto reader = node->content->openReader(o);
    if (!reader) {
        w->reportError(reader.error());
        return;
    }
    core::MountRequest req;
    req.what = ss(title()) + " (" + ss(fsName()) + ") in " + w->current()->descriptor.name;
    // The directory (and, on macOS, the Finder volume) takes the filesystem's label, else the node's name.
    req.name = node->content->info().label.empty() ? ss(title()) : node->content->info().label;
    req.sourceId = w->current()->descriptor.id;
    req.nodePath = m_path;
    auto rec = w->mounts().mount(std::move(*reader), std::move(req));
    if (!rec) {
        w->reportError(rec.error());
        return;
    }
    MountsModel::instance()->reload();
    Q_EMIT mounted(drstein::ui::pathText(rec->mountpoint));
}

} // namespace drstein::ui
