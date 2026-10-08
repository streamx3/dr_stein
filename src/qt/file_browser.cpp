// SPDX-License-Identifier: MIT
#include "file_browser.hpp"

#include "drstein/core/format.hpp"
#include "job_runner.hpp"
#include "mounts_model.hpp"
#include "stein/core/strings.hpp"
#include "util.hpp"
#include "workspace.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

namespace drstein::ui {

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

void FileBrowser::setBusy(bool b) {
    if (b == m_busy) return;
    m_busy = b;
    Q_EMIT changed();
}

void FileBrowser::reload() { openSubvolume({}, {}); }

void FileBrowser::openSubvolume(const QString& volume, const QString& snapshot) {
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

void FileBrowser::listCurrent() {
    if (!m_browser) {
        m_entries.set({});
        updatePreview();
        Q_EMIT changed();
        Q_EMIT selectionChanged();
        return;
    }
    auto listing = m_browser->listCurrent();
    if (!listing) {
        Workspace::instance()->reportError(listing.error());
        m_entries.set({});
    } else {
        m_entries.set(std::move(listing->entries));
    }
    m_tree.setCurrentPath(pathText());
    updatePreview();
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

void FileBrowser::updatePreview() {
    m_previewKind = "none";
    m_previewText.clear();
    m_previewHex.clear();
    m_previewImage.clear();
    m_previewNote.clear();
    m_hashText = "not computed";
    const int i = m_entries.selected();
    if (!m_browser || i < 0) return;
    const auto& e = m_entries.entries()[static_cast<std::size_t>(i)];
    auto pv = core::preview(m_browser->reader(), e.entry.inode, e.stat);
    if (!pv) {
        m_previewKind = "unreadable";
        m_previewNote = qs(pv.error().message());
        return;
    }
    using core::PreviewKind;
    switch (pv->kind) {
    case PreviewKind::Empty: m_previewKind = "empty"; break;
    case PreviewKind::Text: m_previewKind = "text"; break;
    case PreviewKind::Hex: m_previewKind = "hex"; break;
    case PreviewKind::Image: m_previewKind = "image"; break;
    case PreviewKind::Unreadable: m_previewKind = "unreadable"; break;
    }
    m_previewNote = qs(pv->note);
    if (pv->kind == PreviewKind::Text) m_previewText = qs(pv->text);
    if (!pv->bytes.empty()) {
        const std::size_t n = std::min<std::size_t>(pv->bytes.size(), 4096);
        m_previewHex = qs(stein::hexDump(std::span<const std::byte>(pv->bytes.data(), n)));
        if (pv->bytes.size() > n) m_previewHex += "\n… first 4 KiB shown";
    }
    if (pv->kind == PreviewKind::Image) {
        const QByteArray raw(reinterpret_cast<const char*>(pv->bytes.data()), static_cast<qsizetype>(pv->bytes.size()));
        m_previewImage = "data:image/" + qs(pv->imageFormat) + ";base64," + QString::fromLatin1(raw.toBase64());
    }
    if (!e.linkTarget.empty()) m_previewNote = "→ " + qs(e.linkTarget);
}

void FileBrowser::activate(int row) {
    if (!m_browser || row < 0 || row >= m_entries.rowCount() || m_busy) return;
    const auto& e = m_entries.entries()[static_cast<std::size_t>(row)];
    if (e.isDir()) enter(qs(e.entry.name));
    else select(row);
}

void FileBrowser::select(int row) {
    m_entries.setSelected(row);
    updatePreview();
    Q_EMIT selectionChanged();
    Q_EMIT changed();
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
    m_browser->up();
    listCurrent();
}

void FileBrowser::jumpTo(int crumb) {
    if (!m_browser || m_busy) return;
    m_browser->jumpTo(static_cast<std::size_t>(std::max(0, crumb)));
    listCurrent();
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
        [this, out, i](bool ok, const stein::Error&) {
            setBusy(false);
            if (ok && m_entries.selected() == i) {
                m_hashText = qs(*out);
                Q_EMIT selectionChanged();
            }
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
    if (!m_browser || !w->tree() || !w->current()) return;
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
    const std::string what = ss(title()) + " (" + ss(fsName()) + ") in " + w->current()->descriptor.name;
    auto rec = w->mounts().mount(std::move(*reader), what);
    if (!rec) {
        w->reportError(rec.error());
        return;
    }
    MountsModel::instance()->reload();
    Q_EMIT mounted(drstein::ui::pathText(rec->mountpoint));
}

} // namespace drstein::ui
