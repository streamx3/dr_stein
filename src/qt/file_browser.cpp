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
};

namespace {

constexpr int kDebounceMs = 80;   // a pause this long in keyboard movement asks for the preview
constexpr int kGraceMs = 150;     // the previous preview stays up this long while the next one loads
// A request that waits this long behind one that was given up gets a new worker: a decode or a
// read on failing media cannot be interrupted, and would hold every later preview back.
constexpr int kStallMs = 10000;
constexpr int kMaxAbandonedWorkers = 3;   // per browser; a disk that swallows that many is left alone
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

QVariant EntriesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& e = m_entries[static_cast<std::size_t>(index.row())];
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

void EntriesModel::set(std::vector<core::Entry> entries) {
    beginResetModel();
    m_entries = std::move(entries);
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
    connect(Settings::instance(), &Settings::previewFilesChanged, this, &FileBrowser::previewSettingChanged);
    connect(Workspace::instance(), &Workspace::uiLockedChanged, this, [this] {
        if (!m_previewHeld || previewHeld()) return;
        sendRequest();
        Q_EMIT selectionChanged();
    });
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
    m_browser.reset();
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

void FileBrowser::listCurrent(const std::string& reselect) {
    if (!m_browser) {
        m_entries.set({});
    } else if (auto listing = m_browser->listCurrent(); !listing) {
        Workspace::instance()->reportError(listing.error());
        m_entries.set({});
    } else {
        m_entries.set(std::move(listing->entries));
        if (!reselect.empty()) {
            const auto& rows = m_entries.entries();
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const core::Entry& e) { return e.isDir() && e.entry.name == reselect; });
            if (it != rows.end()) m_entries.setSelected(static_cast<int>(it - rows.begin()));
        }
    }
    if (m_browser) m_tree.setCurrentPath(pathText());
    m_hashText = "not computed";
    refreshPreview(PreviewStart::Now);
    Q_EMIT changed();
    Q_EMIT selectionChanged();
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
        PreviewContent off;
        off.kind = "off";
        return settlePreview(off);
    }
    if (const auto known = cachedPreview(key)) return settlePreview(*known);

    m_debounce.stop();
    cancelTicket();
    if (std::exchange(m_previewHeld, false)) showLoading();
    m_request = PreviewRequest{key, e->entry.inode, e->stat};
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
    m_workerMayLag = true;
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
    m_ticket = m_loader->request(m_request->inode, m_request->stat);
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
    m_workerMayLag = false;
    m_loader = std::make_unique<core::PreviewLoader>(std::move(*reader), core::PreviewLimits{}, &renderPreview, [this, epoch](core::PreviewResult result) {
        // On the worker, under the loader's lock: `this` is only the address the result is posted
        // to, which is safe because nothing is delivered once ~PreviewLoader has returned. The
        // file's bytes end here; what is shown crosses over.
        const quint64 ticket = result.ticket;
        auto content = result.preview ? std::static_pointer_cast<const PreviewContent>(result.decoded) : nullptr;
        QString failure = result.preview ? QString() : qs(result.preview.error().message());
        result = {};
        QMetaObject::invokeMethod(this, [this, epoch, ticket, content = std::move(content), failure = std::move(failure)] { previewArrived(epoch, ticket, content, failure); },
                                  Qt::QueuedConnection);
    });
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
    m_workerMayLag = false;
    m_abandonedWorkers = 0;
    m_loaderError.clear();
    m_cache.clear();
}

void FileBrowser::previewArrived(quint64 epoch, quint64 ticket, std::shared_ptr<const PreviewContent> content, const QString& failure) {
    if (epoch != m_epoch || ticket != m_ticket) return;
    m_ticket = 0;
    m_workerMayLag = false;
    if (!content) {
        auto failed = std::make_shared<PreviewContent>();
        failed->kind = "unreadable";
        failed->note = failure.isEmpty() ? QString("the preview could not be made") : failure;
        content = std::move(failed);
    } else if (content->kind != "unreadable" && m_request) {
        // A failed read is not remembered: coming back to the file tries again.
        rememberPreview(m_request->key, content);
    }
    settlePreview(*content);
    Q_EMIT selectionChanged();
}

void FileBrowser::requestStalled() {
    if (!m_ticket || !m_workerMayLag || m_abandonedWorkers >= kMaxAbandonedWorkers) return;
    // The old worker is left to finish what it is in; it owns its reader and delivers nothing more.
    ++m_abandonedWorkers;
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
    if (e.isDir()) enter(qs(e.entry.name));
    else select(row);
}

void FileBrowser::selectRow(int row, PreviewStart start) {
    const int before = m_entries.selected();
    m_entries.setSelected(row);
    const bool moved = m_entries.selected() != before;
    if (moved) m_hashText = "not computed";
    // A click on the row that is selected already changes nothing that is shown, except that it
    // hurries a preview along and tries an unreadable file once more.
    if (moved || m_previewLoading || m_previewKind == "unreadable") refreshPreview(start);
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
    if (e && e->isDir()) enter(qs(e->entry.name));
}

void FileBrowser::enter(const QString& name) {
    if (!m_browser || m_busy) return;
    if (auto r = m_browser->enter(ss(name)); !r) {
        Workspace::instance()->reportError(r.error());
        return;
    }
    listCurrent();
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

void FileBrowser::copyOut(int row, const QUrl& directory) {
    if (!m_browser || row < 0 || row >= m_entries.rowCount() || m_busy) return;
    const auto e = m_entries.entries()[static_cast<std::size_t>(row)];
    core::Browser* browser = &*m_browser;
    const std::filesystem::path dest = pathOf(directory);
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Copying out " + qs(e.entry.name), "browse",
        [browser, e, dest](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            report.start();
            report.addDetail("destination", (dest / e.entry.name).string());
            auto stats = core::copyOut(browser->reader(), e.entry.inode, e.entry.name, dest, progress);
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

void FileBrowser::copyAll(const QUrl& directory) {
    if (!m_browser || m_busy) return;
    core::Browser* browser = &*m_browser;
    const stein::fs::Inode dir = m_browser->current();
    const std::filesystem::path dest = pathOf(directory);
    const std::string name = m_browser->breadcrumbs().size() > 1 ? m_browser->breadcrumbs().back().name : ss(title());
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Copying " + qs(name) + " out", "browse",
        [browser, dir, dest, name](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            report.start();
            auto stats = core::copyOut(browser->reader(), dir, name, dest, progress);
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
    QStringList urls;
    if (!m_browser || m_busy) return urls;
    quint64 total = 0;
    std::vector<core::Entry> picked;
    for (int row : rows) {
        if (row < 0 || row >= m_entries.rowCount()) continue;
        const auto& e = m_entries.entries()[static_cast<std::size_t>(row)];
        if (e.stat.type == stein::fs::FileType::File) total += e.stat.size;
        picked.push_back(e);
    }
    if (picked.empty()) return urls;
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
