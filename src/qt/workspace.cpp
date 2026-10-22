// SPDX-License-Identifier: MIT
#include "stein/block/slice_device.hpp"
#include "workspace.hpp"

#include "drstein/core/format.hpp"
#include "job_runner.hpp"
#include "settings.hpp"
#include "stein/core/strings.hpp"
#include "util.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QUrl>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include <cerrno>
#include <set>
#include <utility>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace drstein::ui {

namespace {
Workspace* g_instance = nullptr;

// A secret leaves memory overwritten, not merely freed: the heap (or the string's own
// small buffer) would otherwise keep it until reused. The volatile pointer stops the
// compiler from dropping stores to a string it sees dying.
void wipe(std::string& s) {
    volatile char* p = s.data();
    for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
    s.shrink_to_fit();
}

void wipeAll(std::vector<std::string>& list) {
    for (auto& s : list) wipe(s);
    list.clear();
}

void addUnique(std::vector<std::string>& list, const std::string& p) {
    for (const auto& x : list)
        if (x == p) return;
    list.push_back(p);
}

void forgetOne(std::vector<std::string>& list, const std::string& p) {
    for (auto it = list.begin(); it != list.end(); ++it)
        if (*it == p) {
            wipe(*it);
            list.erase(it);
            return;
        }
}

std::string uuidOf(const stein::probe::Node& node) { return node.content ? node.content->info().uuid : std::string{}; }

// Depth-first search for the node whose content carries `uuid`; fills `path` on success.
const stein::probe::Node* findByUuid(const stein::probe::Node& node, const std::string& uuid, core::NodePath& path) {
    if (uuid.empty()) return nullptr;
    if (uuidOf(node) == uuid) return &node;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        path.push_back(static_cast<int>(i));
        if (const auto* found = findByUuid(node.children[i], uuid, path)) return found;
        path.pop_back();
    }
    return nullptr;
}

QVariantMap toast(const QString& title, const QString& message, const QString& severity) {
    QVariantMap e;
    e["title"] = title;
    e["message"] = message;
    e["hint"] = "";
    e["severity"] = severity;
    return e;
}
} // namespace

Workspace::Workspace(QObject* parent) : QObject(parent) {
    // The singleton's destructor is never reached through Qt's teardown; quitting is the
    // moment the passphrases must go.
    if (auto* app = QCoreApplication::instance()) connect(app, &QCoreApplication::aboutToQuit, this, [this] { clearPassphrases(); });
    // An elevated relaunch names the source to come back to; main() has had its turn with the
    // command line by the time the event loop runs this.
    QTimer::singleShot(0, this, [this] { restoreRelaunchSelection(); });
    connect(JobRunner::instance(), &JobRunner::runningChanged, this, &Workspace::uiLockedChanged);
    connect(Settings::instance(), &Settings::interactDuringJobsChanged, this, &Workspace::uiLockedChanged);
    connect(Settings::instance(), &Settings::expertModeChanged, this, [this] {
        rebuildRows();
        Q_EMIT selectionChanged();
    });
    // Recent image files reappear in the sidebar (those that still exist).
    for (const QString& p : Settings::instance()->recentImages()) {
        if (!QFileInfo::exists(p)) continue;
        if (auto d = core::describeImageFile(ss(p))) m_images.push_back(*d);
    }
    refresh();
}

Workspace::~Workspace() { clearPassphrases(); }

Workspace* Workspace::instance() {
    if (!g_instance) g_instance = new Workspace();
    return g_instance;
}

Workspace* Workspace::create(QQmlEngine*, QJSEngine*) {
    Workspace* w = instance();
    QQmlEngine::setObjectOwnership(w, QQmlEngine::CppOwnership);
    return w;
}

QVariantMap Workspace::errorToVariantPublic(const stein::Error& e) { return errorToVariant(e); }

bool Workspace::elevated() const { return core::isElevated(); }
QString Workspace::platformName() const { return qs(stein::platform::current().name()); }
QString Workspace::revealLabel() const {
    const QString os = QSysInfo::productType();
    return os == "macos" ? "Reveal in Finder" : os == "windows" ? "Show in Explorer" : "Show in file manager";
}
QString Workspace::version() const { return QStringLiteral(DRSTEIN_VERSION_STRING); }

void Workspace::setView(const QString& v) {
    if (v == m_view) return;
    m_view = v;
    Q_EMIT viewChanged();
}

void Workspace::setBusy(bool b) {
    if (b == m_busy) return;
    m_busy = b;
    Q_EMIT busyChanged();
}

// ---- sources ---------------------------------------------------------------------

void Workspace::refresh() {
    std::set<std::string> knownDisks;
    for (const auto& s : m_sources.items())
        if (s.kind == core::SourceKind::Disk) knownDisks.insert(s.id);
    std::vector<core::SourceDescriptor> items;
    auto disks = core::listDisks(stein::platform::current());
    if (disks) items = std::move(*disks);
    else Q_EMIT error(errorToVariant(disks.error()));
    for (const auto& img : m_images) items.push_back(img);
    m_sources.setItems(std::move(items));
    m_sources.setSelected(m_selectedSourceId);
    // A disk that is gone (unplugged) takes its passphrases with it; plugging it back in asks again.
    if (disks)
        for (const auto& id : knownDisks)
            if (!m_sources.find(id)) forgetPassphrases(id);
    // The open source may have vanished (unplugged); keep showing it until the user picks another.
    // A refresh also re-reads the open disk: its mounts and its table may have changed.
    if (m_current && !JobRunner::instance()->running()) reprobe();
    else refreshMounts();
}

bool Workspace::uiLocked() const {
    JobRunner* j = JobRunner::instance();
    if (!j->running() || Settings::instance()->interactDuringJobs()) return false;
    return j->kind() != "probe" && j->kind() != "usage";
}

void Workspace::setRestoreTarget(const QString& sourceId) {
    if (qs(m_sources.target()) == sourceId) return;
    m_sources.setTarget(ss(sourceId));
    Q_EMIT restoreTargetChanged();
}

void Workspace::refreshMounts() {
    m_osMounts.clear();
    const core::SourceDescriptor* d = m_current ? &m_current->descriptor : m_sources.find(m_currentId);
    if (d && d->kind == core::SourceKind::Disk)
        if (auto mounts = stein::platform::current().mounts(d->path))
            for (const auto& m : *mounts) m_osMounts.push_back({m.source, m.target, m.fsType, m.readOnly});
    rebuildRows();
    updateDetails();
    Q_EMIT mountsChanged();
}

void Workspace::unmountSelected() {
    if (!m_current) return;
    const QString mp = m_details.mountpoint();
    if (mp.isEmpty()) return;
    if (auto own = m_mounts.findByMountpoint(std::filesystem::path(ss(mp)))) {
        if (auto r = m_mounts.unmount(own->id); !r) Q_EMIT error(errorToVariant(r.error()));
        refreshMounts();
        return;
    }
    if (!m_current->descriptor.disk) return;
    stein::platform::MountInfo info;
    for (const auto& m : m_osMounts)
        if (qs(m.mountpoint) == mp) {
            info.source = m.device;
            info.target = m.mountpoint;
            info.fsType = m.fsType;
            info.readOnly = m.readOnly;
        }
    if (auto r = stein::platform::current().unmount(info, false); !r) {
        Q_EMIT error(errorToVariant(r.error()));
        return;
    }
    refreshMounts();
}

void Workspace::repairSelected() {
    if (!m_current) return;
    const auto* node = core::nodeAt(m_current->tree, m_selectedPath);
    if (!node || !node->content) return;
    if (JobRunner::instance()->running()) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running")));
        return;
    }
    core::OpenOptions o;
    o.writable = true;
    auto dev = core::openDevice(m_current->descriptor, o);
    if (!dev) {
        Q_EMIT error(errorToVariant(dev.error()));
        return;
    }
    // The node's device may be a slice of the root; repair through the same extents, writable.
    std::shared_ptr<stein::BlockDevice> target = *dev;
    if (node->device != m_current->device) {
        const auto extents = node->device->extentsOnParent();
        if (extents.size() != 1 || node->device->parent() != m_current->device) {
            Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Unsupported, "repairs inside containers or volumes cannot be written back yet")));
            return;
        }
        auto slice = stein::SliceDevice::create(target, extents.front(), node->device->name());
        if (!slice) {
            Q_EMIT error(errorToVariant(slice.error()));
            return;
        }
        target = *slice;
    }
    if (auto r = node->content->repair(*target); !r) {
        Q_EMIT error(errorToVariant(r.error()));
        return;
    }
    reprobe();
}

void Workspace::openImage(const QUrl& url) { openImagePath(url.isLocalFile() ? url.toLocalFile() : url.toString()); }

void Workspace::addImagePath(const QString& path) {
    const std::string id = "image:" + ss(QFileInfo(path).absoluteFilePath());
    const auto known = imagePassphrasesFor(id);
    auto d = core::describeImageFile(ss(path), known.empty() ? std::string{} : known.front());
    if (!d) {
        Q_EMIT error(errorToVariant(d.error()));
        return;
    }
    bool replaced = false;
    for (auto& img : m_images)
        if (img.id == d->id) {
            img = *d;
            replaced = true;
        }
    if (!replaced) m_images.push_back(*d);
    m_sources.add(*d);
    Settings::instance()->addRecentImage(qs(d->path));
    Settings::instance()->setLastImageDir(QFileInfo(path).absolutePath());
}

void Workspace::openImagePath(const QString& path) {
    addImagePath(path);
    const std::string id = "image:" + ss(QFileInfo(path).absoluteFilePath());
    if (m_sources.find(id)) select(qs(id));
}

void Workspace::closeImage(const QString& idIn) {
    const std::string id = ss(idIn);
    if (JobRunner::instance()->running()) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running; wait for it to finish")));
        return;
    }
    for (auto it = m_images.begin(); it != m_images.end(); ++it)
        if (it->id == id) {
            Settings::instance()->forgetRecentImage(qs(it->path));
            m_images.erase(it);
            break;
        }
    m_sources.remove(id);
    forgetPassphrases(id);
    if (m_currentId == id) setCurrent(std::nullopt, {}, {});
}

std::vector<std::string> Workspace::containerPassphrasesFor(const std::string& id) const {
    auto it = m_passphrases.find(id);
    return it == m_passphrases.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> Workspace::imagePassphrasesFor(const std::string& id) const {
    auto it = m_imagePassphrases.find(id);
    return it == m_imagePassphrases.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> Workspace::passphrasesFor(const std::string& id) const {
    std::vector<std::string> out = imagePassphrasesFor(id);
    for (const auto& p : containerPassphrasesFor(id)) addUnique(out, p);
    return out;
}

core::MasterKeys Workspace::masterKeysFor(const std::string& id) const {
    auto it = m_masterKeys.find(id);
    return it == m_masterKeys.end() ? core::MasterKeys{} : it->second;
}

void Workspace::forgetPassphrases(const std::string& id) {
    if (auto it = m_passphrases.find(id); it != m_passphrases.end()) {
        wipeAll(it->second);
        m_passphrases.erase(it);
    }
    if (auto it = m_imagePassphrases.find(id); it != m_imagePassphrases.end()) {
        wipeAll(it->second);
        m_imagePassphrases.erase(it);
    }
    if (auto it = m_masterKeys.find(id); it != m_masterKeys.end()) {
        core::wipeMasterKeys(it->second);
        m_masterKeys.erase(it);
    }
    if (m_pendingUnlock && m_pendingUnlock->sourceId == id) {
        wipe(m_pendingUnlock->passphrase);
        m_pendingUnlock.reset();
    }
}

void Workspace::clearPassphrases() {
    for (auto& [id, list] : m_passphrases) wipeAll(list);
    m_passphrases.clear();
    for (auto& [id, list] : m_imagePassphrases) wipeAll(list);
    m_imagePassphrases.clear();
    for (auto& [id, keys] : m_masterKeys) core::wipeMasterKeys(keys);
    m_masterKeys.clear();
    if (m_pendingUnlock) wipe(m_pendingUnlock->passphrase);
    m_pendingUnlock.reset();
}

void Workspace::select(const QString& idIn) {
    const std::string id = ss(idIn);
    if (id == m_currentId && m_current) return;
    if (JobRunner::instance()->running()) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running; wait for it to finish before switching")));
        return;
    }
    if (!m_sources.find(id)) return;
    m_selectedSourceId = id;
    m_sources.setSelected(id);
    openSourceAsync(id, false);
}

void Workspace::openSourceAsync(const std::string& id, bool silentOnError) {
    const core::SourceDescriptor* d = m_sources.find(id);
    if (!d) return;
    core::SourceDescriptor desc = *d;
    core::OpenOptions options;
    options.passphrases = containerPassphrasesFor(id);
    options.imagePassphrases = imagePassphrasesFor(id);
    options.tcrypt = std::exchange(m_tcryptOnce, false);   // one probe, then back to the cheap default
    options.masterKeys = masterKeysFor(id);                 // unlocked once this session: the probe skips the KDF
    // What the job hands back: the open itself, the keys the passphrases produced on the way (for the
    // cache) and whether the job's Cancel reached a key derivation. Written on the job thread, read in
    // the done handler once the thread has posted it.
    struct ProbeRun {
        stein::Expected<core::OpenedSource> opened = stein::fail(stein::ErrorCategory::Internal, "not run");
        core::MasterKeys unlocked;
        bool cancelled = false;
    };
    auto run = std::make_shared<ProbeRun>();
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Probing " + qs(desc.name), "probe",
        [desc, options, run](stein::Progress& progress, stein::Report& report) mutable -> stein::Expected<void> {
            progress.setPhase("Probing", 0, "steps");
            report.start();
            // The job's Cancel reaches the KDF through this poll: the container is left locked and the
            // probe finishes the rest of the device, so there is still a tree to show.
            options.cancelled = [&progress, run] {
                if (!progress.isCancelled()) return false;
                run->cancelled = true;
                return true;
            };
            options.onUnlocked = [run](const std::string& uuid, const std::vector<std::uint8_t>& key) { run->unlocked[uuid] = key; };
            run->opened = core::openSource(desc, options);
            // This closure outlives the job (the thread object stays until the next one starts).
            core::wipeMasterKeys(options.masterKeys);
            wipeAll(options.passphrases);
            wipeAll(options.imagePassphrases);
            if (!run->opened) {
                report.addLine(run->opened.error().toString());
                report.finish(stein::ReportStatus::Error);
                return stein::fail(run->opened.error());
            }
            report.addDetail("device", run->opened->device->name());
            report.finish(stein::ReportStatus::Success);
            return {};
        },
        [this, id, run, silentOnError](bool ok, const stein::Error& err) {
            setBusy(false);
            // Keys the passphrases produced join the session cache: from here on this source re-probes
            // without the KDF. A source that vanished meanwhile (unplugged mid-probe) keeps nothing.
            if (m_sources.find(id)) {
                auto& cache = m_masterKeys[id];
                for (auto& [uuid, key] : run->unlocked) {
                    if (auto it = cache.find(uuid); it != cache.end()) core::wipeMasterKey(it->second);
                    cache[uuid] = std::move(key);
                }
            }
            core::wipeMasterKeys(run->unlocked);
            const bool cancelled = run->cancelled;
            if (ok) {
                setCurrent(std::move(*run->opened), {}, id);
                finishPendingUnlock(true, cancelled);
                if (!m_relaunchSource.empty() && m_relaunchSource == id) {
                    m_relaunchSource.clear();
                    promptAfterRelaunch();
                }
            } else {
                QVariantMap e = errorToVariant(err);
                // macOS answers EACCES when the user id may not open the node and EPERM when
                // the privacy system (TCC) vetoed it; the latter needs Full Disk Access, not root.
                const bool privacyVeto = QSysInfo::productType() == "macos" && err.category() == stein::ErrorCategory::Permission && (err.osCode() == EPERM || elevated());
                if (privacyVeto) {
                    e["title"] = "Blocked by macOS privacy settings";
                    e["message"] = qs(err.message());
                    e["hint"] = QString("macOS lets only apps with Full Disk Access read disks directly. Open Privacy & Security, switch on Dr Stein there (drag the app into the list if it is missing), then open the disk again. ") +
                                (elevated() ? "Root does not bypass this; the grant is per app, and when Dr Stein is started from a terminal it is the terminal's grant that counts."
                                            : "Opening the disk still needs elevation afterwards.");
                    e["fullDiskAccess"] = true;
                    e["needsElevation"] = !elevated();
                } else if (err.category() == stein::ErrorCategory::Permission && elevated()) {
                    e["title"] = "Blocked by the system, not by permissions";
                    e["hint"] = "Dr Stein is already elevated; the device is held back by the operating system itself (a mount, a policy, or a kernel driver).";
                    e["needsElevation"] = false;
                }
                setCurrent(std::nullopt, e, id);
                finishPendingUnlock(false, cancelled);
                if (silentOnError) return;
                if (err.category() == stein::ErrorCategory::Permission && m_sources.find(id) && m_sources.find(id)->image && m_sources.find(id)->image->encrypted)
                    Q_EMIT passphraseNeeded(unlockLabel());
            }
        });
    // The job has its own copies; this frame's secrets end here, started or not.
    core::wipeMasterKeys(options.masterKeys);
    wipeAll(options.passphrases);
    wipeAll(options.imagePassphrases);
    if (!started) {
        setBusy(false);
        if (m_pendingUnlock) wipe(m_pendingUnlock->passphrase);
        m_pendingUnlock.reset();
    }
}

void Workspace::finishPendingUnlock(bool ok, bool cancelled) {
    if (cancelled) {
        // The job's Cancel stopped a key derivation, so nothing was refuted. The user said stop,
        // though: the passphrase of the unlock in flight goes too, or the next hot-plug refresh would
        // quietly start the same slow unlock all over again.
        const bool container = m_pendingUnlock && !m_pendingUnlock->image;
        Q_EMIT error(toast("Unlock cancelled", container ? "Nothing was unlocked on " + qs(m_pendingUnlock->name) + "." : "Nothing was unlocked; locked containers stay as they are.", "info"));
        if (container) {
            forgetOne(m_passphrases[m_pendingUnlock->sourceId], m_pendingUnlock->passphrase);
            wipe(m_pendingUnlock->passphrase);
            m_pendingUnlock.reset();
            return;
        }
    }
    if (!m_pendingUnlock) return;
    PendingUnlock pu = std::move(*m_pendingUnlock);
    m_pendingUnlock.reset();
    auto wrong = [&] {
        forgetOne(pu.image ? m_imagePassphrases[pu.sourceId] : m_passphrases[pu.sourceId], pu.passphrase);
        if (pu.tcrypt) Q_EMIT error(toast("Nothing unlocked", "No VeraCrypt or TrueCrypt header on " + qs(pu.name) + " opens with that passphrase.", "warning"));
        else Q_EMIT error(toast("Wrong passphrase", "Wrong passphrase for " + qs(pu.name) + ".", "error"));
    };
    if (pu.image) {
        // The file either opened or did not; with the image passphrases apart from the
        // containers' there is nothing else a failed open can mean.
        if (!ok) wrong();
    } else if (ok && m_current && m_currentId == pu.sourceId) {
        // The node the user unlocked: by path while the tree still agrees on what sits there, else
        // by uuid (a probe can shift indices when a region appears or disappears).
        core::NodePath path = pu.path;
        const stein::probe::Node* node = core::nodeAt(m_current->tree, path);
        if (!node || uuidOf(*node) != pu.uuid) {
            path.clear();
            node = findByUuid(m_current->tree, pu.uuid, path);
        }
        if (node) {
            bool opened = false;
            for (std::size_t i = 0; i < node->children.size() && !opened; ++i)
                if (node->children[i].kind == stein::probe::NodeKind::Decrypted) {
                    core::NodePath child = path;
                    child.push_back(static_cast<int>(i));
                    selectPath(pathToVariant(child));   // the tree speaks; no toast
                    opened = true;
                }
            if (!opened) {
                const int row = m_topology.rowForPath(path);
                const bool locked = row >= 0 && m_topology.rows()[static_cast<std::size_t>(row)].isLockedContainer;
                if (locked || pu.tcrypt) wrong();
            }
        }
    }
    wipe(pu.passphrase);
}

void Workspace::promptAfterRelaunch() {
    // Back on the source after the elevated relaunch; the passphrase did not travel, so the
    // first container that wants one asks now, once. Nothing locked: nothing to ask.
    const auto& rows = m_topology.rows();
    for (std::size_t row = 0; row < rows.size(); ++row)
        if (rows[row].isLockedContainer) {
            selectRow(static_cast<int>(row));
            Q_EMIT passphraseNeeded(unlockLabel());
            return;
        }
}

void Workspace::restoreRelaunchSelection() {
    const QByteArray raw = qgetenv("DRSTEIN_SOURCE");
    if (raw.isEmpty()) return;
    qunsetenv("DRSTEIN_SOURCE");   // once; a plain restart from this process must not replay it
    const std::string id = raw.toStdString();
    if (!m_sources.find(id)) return;
    auto go = [this, id] {
        // main() opens the images from the command line itself; when the one to come back to is
        // already open, select() would return early and the prompt would never come.
        if (m_current && m_currentId == id) {
            promptAfterRelaunch();
            return;
        }
        m_relaunchSource = id;
        select(qs(id));
    };
    // main() may have started the probe of an image given on the command line; let it land first.
    if (!JobRunner::instance()->running()) {
        go();
        return;
    }
    auto* ctx = new QObject(this);
    connect(JobRunner::instance(), &JobRunner::runningChanged, ctx, [this, ctx, go] {
        if (JobRunner::instance()->running()) return;
        ctx->deleteLater();
        QTimer::singleShot(0, this, go);   // after the finished job's own done handler
    });
}

void Workspace::setCurrent(std::optional<core::OpenedSource> source, QVariantMap err, const std::string& id) {
    m_current = std::move(source);
    m_currentId = id;
    m_currentError = std::move(err);
    m_selectedPath.clear();
    m_selectedMetadata = -1;
    m_segments.clear();
    m_segmentData.clear();
    m_osMounts.clear();
    if (m_current && m_current->descriptor.kind == core::SourceKind::Disk)
        if (auto mounts = stein::platform::current().mounts(m_current->descriptor.path))
            for (const auto& m : *mounts) m_osMounts.push_back({m.source, m.target, m.fsType, m.readOnly});
    if (m_current) {
        // A descriptor refreshed through the open (e.g. an unlocked image) replaces the sidebar entry.
        m_sources.replace(m_current->descriptor);
        for (auto& img : m_images)
            if (img.id == id) img = m_current->descriptor;
        m_segmentData = core::segments(m_current->tree);
        for (std::size_t i = 0; i < m_segmentData.size(); ++i) {
            const auto& s = m_segmentData[i];
            QVariantMap m;
            m["index"] = static_cast<int>(i);
            m["path"] = pathToVariant(s.path);
            m["offset"] = static_cast<qulonglong>(s.region.offset);
            m["length"] = static_cast<qulonglong>(s.region.length);
            m["label"] = qs(s.label);
            m["isFree"] = s.isFree;
            m["colorIndex"] = s.colorIndex;
            m_segments.push_back(m);
        }
    }
    rebuildRows();
    Q_EMIT currentChanged();
    if (m_current) {
        m_topology.setSelectedRow(0);
        m_selectedPath.clear();
        updateDetails();
        Q_EMIT selectionChanged();
        Q_EMIT opened(qs(id));
    } else {
        m_details.set({}, false);
        Q_EMIT selectionChanged();
    }
}

void Workspace::rebuildRows() {
    if (!m_current) {
        m_topology.setRows({});
        return;
    }
    auto rows = core::topologyRows(m_current->tree, Settings::instance()->expertMode());
    if (m_current->descriptor.disk) core::annotateOsDevices(rows, m_current->tree, *m_current->descriptor.disk, stein::platform::current().name(), m_osMounts);
    // The OS table knows nothing of the app's own exports; they are found by node.
    for (const auto& m : m_mounts.list()) {
        if (!m.running || m.sourceId != m_currentId) continue;
        for (auto& r : rows)
            if (!r.isMetadata && r.path == m.nodePath && r.mountpoint.empty()) r.mountpoint = m.mountpoint.string();
    }
    m_topology.setRows(std::move(rows));
    const int row = m_selectedMetadata >= 0 ? -1 : m_topology.rowForPath(m_selectedPath);
    m_topology.setSelectedRow(row >= 0 ? row : 0);
}

// ---- selection --------------------------------------------------------------------

void Workspace::selectRow(int row) {
    if (!m_current || row < 0 || row >= m_topology.rowCount()) return;
    const auto& r = m_topology.rows()[static_cast<std::size_t>(row)];
    m_topology.setSelectedRow(row);
    m_selectedPath = r.path;
    m_selectedMetadata = r.isMetadata ? r.metadataIndex : -1;
    updateDetails();
    Q_EMIT selectionChanged();
}

void Workspace::selectPath(const QVariantList& path) {
    if (!m_current) return;
    const core::NodePath p = pathFromVariant(path);
    const int row = m_topology.rowForPath(p);
    if (row >= 0) selectRow(row);
}

void Workspace::selectSegment(int segment) {
    if (segment < 0 || static_cast<std::size_t>(segment) >= m_segmentData.size()) return;
    selectPath(pathToVariant(m_segmentData[static_cast<std::size_t>(segment)].path));
}

int Workspace::selectedSegment() const {
    if (!m_current) return -1;
    const int row = m_topology.selectedRow();
    if (row < 0) return -1;
    return m_topology.rows()[static_cast<std::size_t>(row)].segment;
}

QVariantList Workspace::selectedPathVariant() const { return pathToVariant(m_selectedPath); }

void Workspace::updateDetails() {
    m_tcryptCandidate = false;
    if (!m_current) {
        m_details.set({}, false);
        return;
    }
    if (m_selectedMetadata >= 0) m_details.set(core::metadataDetails(m_current->tree, m_selectedMetadata), true);
    else {
        core::NodeDetails d = core::nodeDetails(m_current->tree, m_selectedPath);
        if (m_current->descriptor.disk) core::annotateOsDevice(d, m_current->tree, m_selectedPath, *m_current->descriptor.disk, stein::platform::current().name(), m_osMounts);
        if (d.mountpoint.empty())
            if (auto own = m_mounts.find(m_currentId, m_selectedPath); own && own->running) {
                d.mountpoint = own->mountpoint.string();
                d.rows.insert(d.rows.begin(), {"mounted at", d.mountpoint + " · by Dr Stein, read-only", false});
                d.canMount = false;
            }
        if (const auto* node = core::nodeAt(m_current->tree, m_selectedPath)) {
            // What the probe's VeraCrypt trial would look at: bytes nothing else claims, big enough for a header pair.
            using stein::probe::NodeKind;
            m_tcryptCandidate = node->device && !node->content && !node->table && node->children.empty() && node->kind != NodeKind::Free && node->region.length >= 256 * stein::KiB;
        }
        if (m_selectedPath.empty()) {
            d.title = m_current->descriptor.title;
            if (const auto& img = m_current->descriptor.image) {
                // The file's own facts come first; the table's follow.
                std::vector<core::DetailRow> rows;
                rows.push_back({"image", img->formatName + (img->variant.empty() ? "" : " " + img->variant) + (img->partitionImage ? " · partition image" : ""), false});
                if (!img->createdText.empty()) rows.push_back({"created", img->createdText, false});
                rows.push_back({"on disk", core::sizeText(img->storedBytes) + " for " + core::sizeText(img->virtualSize) + (img->segments > 1 ? " in " + std::to_string(img->segments) + " files" : ""), false});
                if (!img->sourceName.empty()) rows.push_back({"source", img->sourceName, false});
                if (img->partitionImage) rows.push_back({"from", img->provenanceText, false});
                if (!img->storedHash.empty()) rows.push_back({"hash", img->storedHash});
                rows.insert(rows.end(), d.rows.begin(), d.rows.end());
                d.rows = std::move(rows);
            }
        }
        m_details.set(d, true);
    }
}

void Workspace::computeUsage() {
    if (!m_current || m_selectedMetadata >= 0) return;
    const stein::probe::Node* node = core::nodeAt(m_current->tree, m_selectedPath);
    if (!node || !node->content) return;
    if (JobRunner::instance()->running()) return;
    const core::NodePath path = m_selectedPath;
    auto out = std::make_shared<stein::Expected<core::Usage>>(stein::fail(stein::ErrorCategory::Internal, "not run"));
    const std::string fsName = std::string(stein::fs::displayName(node->content->type()));
    JobRunner::instance()->start(
        "Reading allocation map", "usage",
        [node, out](stein::Progress& progress, stein::Report&) -> stein::Expected<void> {
            progress.setPhase("Reading allocation map", 0, "steps");
            *out = core::usageOf(*node);
            if (!*out) return stein::fail(out->error());
            return {};
        },
        [this, path, out, fsName](bool ok, const stein::Error&) {
            if (!ok || !*out || path != m_selectedPath) return;
            m_details.setUsage((*out)->fraction(), qs(fsName + " used"), qs(core::sizeText((*out)->usedBytes) + " of " + core::sizeText((*out)->totalBytes)));
        });
}

// ---- unlocking and re-probing --------------------------------------------------------

QString Workspace::unlockLabel() const {
    const std::string id = m_currentId.empty() ? m_selectedSourceId : m_currentId;
    const auto* d = m_sources.find(id);
    const std::string source = d ? d->name : std::string{};
    if (!m_current || m_currentId != id) return qs(d && d->image && d->image->encrypted ? source + " (encrypted image)" : source);
    const auto* node = core::nodeAt(m_current->tree, m_selectedPath);
    if (!node || m_selectedMetadata >= 0) return qs(source);
    std::string what = m_selectedPath.empty() ? source : stein::toLower(core::kindLabel(*node)) + " on " + source;   // "partition 2 on disk5"
    if (const std::string chain = node->content ? core::contentChain(*node) : std::string{}; !chain.empty()) what += " (" + chain + ")";   // "(LUKS2)"
    return qs(what);
}

void Workspace::unlock(const QString& passphrase) {
    const std::string id = m_currentId.empty() ? m_selectedSourceId : m_currentId;
    if (id.empty() || passphrase.isEmpty()) return;
    const std::string p = ss(passphrase);
    const auto* d = m_sources.find(id);
    PendingUnlock pending;
    pending.sourceId = id;
    pending.passphrase = p;
    pending.name = ss(unlockLabel());
    // A locked image file takes the passphrase for itself; once it is open, every further
    // passphrase is for the containers inside.
    pending.image = d && d->image && d->image->locked;
    if (pending.image) {
        addUnique(m_imagePassphrases[id], p);
        // Images that were locked need a fresh describe (manifest, hash) before the open.
        if (auto fresh = core::describeImageFile(d->image->path, p)) {
            m_sources.replace(*fresh);
            for (auto& img : m_images)
                if (img.id == id) img = *fresh;
        }
    } else {
        addUnique(m_passphrases[id], p);
        if (m_current && m_currentId == id && m_selectedMetadata < 0)
            if (const auto* node = core::nodeAt(m_current->tree, m_selectedPath)) {
                pending.path = m_selectedPath;
                pending.uuid = uuidOf(*node);
            }
    }
    if (m_pendingUnlock) wipe(m_pendingUnlock->passphrase);
    m_pendingUnlock = std::move(pending);
    openSourceAsync(id, false);
}

void Workspace::tryVeraCrypt(const QString& passphrase) {
    const std::string id = m_currentId;
    if (id.empty() || !m_current || passphrase.isEmpty() || m_selectedMetadata >= 0) return;
    const auto* node = core::nodeAt(m_current->tree, m_selectedPath);
    if (!node) return;
    const std::string p = ss(passphrase);
    addUnique(m_passphrases[id], p);
    PendingUnlock pending;
    pending.sourceId = id;
    pending.passphrase = p;
    pending.name = ss(unlockLabel());
    pending.path = m_selectedPath;
    pending.uuid = uuidOf(*node);
    pending.tcrypt = true;
    if (m_pendingUnlock) wipe(m_pendingUnlock->passphrase);
    m_pendingUnlock = std::move(pending);
    m_tcryptOnce = true;
    openSourceAsync(id, false);
}

void Workspace::mountSelected() {
    if (!m_current || m_selectedMetadata >= 0) return;
    const auto* node = core::nodeAt(m_current->tree, m_selectedPath);
    if (!node || !node->content) return;
    if (JobRunner::instance()->running()) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running; wait for it before mounting")));
        return;
    }
    if (auto own = m_mounts.find(m_currentId, m_selectedPath); own && own->running) {
        Q_EMIT error(toast("Already mounted", "Readable at " + pathText(own->mountpoint) + ".", "info"));
        return;
    }
    // The mount thread owns its reader; the Browse view keeps its own.
    auto reader = node->content->openReader({});
    if (!reader) {
        Q_EMIT error(errorToVariant(reader.error()));
        return;
    }
    const std::string label = node->content->info().label;
    const std::string nodeName = core::displayName(*node).empty() ? m_current->descriptor.name : core::displayName(*node);
    const std::string fsName = std::string(stein::fs::displayName(node->content->type()));
    core::MountRequest req;
    req.what = (label.empty() ? nodeName : label) + " (" + fsName + ") in " + m_current->descriptor.name;
    req.name = label.empty() ? nodeName : label;
    req.sourceId = m_currentId;
    req.nodePath = m_selectedPath;
    auto rec = m_mounts.mount(std::move(*reader), std::move(req));
    if (!rec) {
        Q_EMIT error(errorToVariant(rec.error()));
        return;
    }
    refreshMounts();
    QVariantMap e = toast("Mounted", "Readable at " + pathText(rec->mountpoint) + " until you unmount it (details card or top bar).", "info");
    e["actionLabel"] = revealLabel();
    e["actionPath"] = pathText(rec->mountpoint);
    Q_EMIT error(e);
}

void Workspace::revealPath(const QString& path) {
    if (path.isEmpty()) return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Io, "could not open " + ss(path) + " in the file manager")));
}

void Workspace::reopenCurrent() {
    const std::string id = m_currentId.empty() ? m_selectedSourceId : m_currentId;
    if (id.empty() || JobRunner::instance()->running()) return;
    openSourceAsync(id, false);
}

void Workspace::reprobe() {
    if (m_currentId.empty()) return;
    if (JobRunner::instance()->running()) return;
    const core::NodePath keep = m_selectedPath;
    openSourceAsync(m_currentId, true);
    // Selection is restored once the probe lands.
    QObject* ctx = new QObject(this);
    connect(this, &Workspace::opened, ctx, [this, ctx, keep](const QString&) {
        if (m_topology.rowForPath(keep) >= 0) selectPath(pathToVariant(keep));
        Q_EMIT probed();
        ctx->deleteLater();
    });
}

// ---- identity strip ------------------------------------------------------------------

QString Workspace::currentId() const { return qs(m_currentId); }
QString Workspace::title() const {
    if (m_current) return qs(m_current->descriptor.title);
    if (const auto* d = m_sources.find(m_currentId)) return qs(d->title);
    return {};
}
QString Workspace::path() const {
    if (m_current) return qs(m_current->descriptor.path);
    if (const auto* d = m_sources.find(m_currentId)) return qs(d->path);
    return {};
}
QString Workspace::identityLine() const {
    if (m_current) return qs(m_current->descriptor.identityLine);
    if (const auto* d = m_sources.find(m_currentId)) return qs(d->identityLine);
    return {};
}
QString Workspace::tableText() const {
    if (!m_current) return {};
    if (!m_current->tree.table) return "no table";
    return qs(core::schemeName(m_current->tree.table->type()));
}
QString Workspace::healthText() const {
    if (!m_current) return {};
    const auto h = m_current->tree.health();
    if (h >= stein::layout::Validity::Error) return "Errors";
    if (h == stein::layout::Validity::Warning) return "Warnings";
    if (m_current->descriptor.image) return m_current->descriptor.image->complete ? "Complete" : "Incomplete";
    return "Health OK";
}
QString Workspace::healthLevel() const {
    if (!m_current) return "ok";
    if (m_current->descriptor.image && !m_current->descriptor.image->complete) return "warning";
    return healthName(m_current->tree.health());
}
QString Workspace::sizeText() const {
    if (m_current) return qs(core::sizeText(m_current->device->size()));
    if (const auto* d = m_sources.find(m_currentId)) return qs(core::sizeText(d->sizeBytes));
    return {};
}
QString Workspace::lastLbaText() const {
    if (!m_current) return {};
    const auto g = m_current->device->geometry();
    return qs(core::lbaText(g.sectors() ? g.sectors() - 1 : 0));
}
bool Workspace::isImage() const {
    const auto* d = m_sources.find(m_currentId);
    return d && d->kind == core::SourceKind::ImageFile;
}
bool Workspace::isDisk() const {
    const auto* d = m_sources.find(m_currentId);
    return d && d->kind == core::SourceKind::Disk;
}
bool Workspace::readOnly() const { return !m_current || m_current->device->isReadOnly(); }
bool Workspace::imageLocked() const {
    const auto* d = m_sources.find(m_currentId);
    return d && d->image && d->image->locked;
}
QStringList Workspace::notes() const {
    QStringList out;
    if (!m_current) return out;
    for (const auto& n : m_current->notes) out << qs(n);
    if (m_current->descriptor.image)
        for (const auto& n : m_current->descriptor.image->notes) out << qs(n);
    return out;
}

namespace {

// Single-quoted for /bin/sh: safe for any path.
QString shQuote(const QString& s) {
    QString out = s;
    out.replace("'", "'\\''");
    return "'" + out + "'";
}

} // namespace

bool Workspace::canRelaunchElevated() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return !elevated();
#else
    return !elevated() && !QStandardPaths::findExecutable("pkexec").isEmpty();
#endif
}

void Workspace::relaunchElevated() {
    if (elevated()) return;
    if (JobRunner::instance()->running()) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running; wait for it before relaunching")));
        return;
    }
    const QString exe = QCoreApplication::applicationFilePath();
    QStringList args;
    for (const auto& img : m_images) args << qs(img.path);
    // The elevated copy has root's settings, not the user's: carry the look and the
    // profiles folder across through the environment the app already honours.
    QStringList env;
    env << "DRSTEIN_PALETTE=" + Settings::instance()->palette() << "DRSTEIN_SCHEME=" + Settings::instance()->colorScheme()
        << "DRSTEIN_CHROME=" + QString(Settings::instance()->customTitleBar() ? "custom" : "native")
        << "DRSTEIN_PROFILES_DIR=" + Settings::instance()->profilesDir();
    if (const QByteArray script = qgetenv("DRSTEIN_ELEVATED_SCRIPT"); !script.isEmpty()) env << "DRSTEIN_SCRIPT=" + QString::fromUtf8(script);
    env << "DRSTEIN_VIEW=" + m_view;
    // The selected source by id (a disk's identity, an image's path), so the copy lands where the
    // user was. Passphrases stay here: the environment of a root process is readable by root's
    // tools and would outlive this one in ps output.
    if (const std::string sel = m_currentId.empty() ? m_selectedSourceId : m_currentId; !sel.empty() && m_sources.find(sel)) env << "DRSTEIN_SOURCE=" + qs(sel);
#if defined(Q_OS_WIN)
    QString params;
    for (const auto& a : args) params += "\"" + a + "\" ";
    for (const auto& e : env) _wputenv_s(reinterpret_cast<const wchar_t*>(e.section('=', 0, 0).utf16()), reinterpret_cast<const wchar_t*>(e.section('=', 1).utf16()));
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"runas", reinterpret_cast<const wchar_t*>(exe.utf16()), reinterpret_cast<const wchar_t*>(params.utf16()), nullptr, SW_SHOWNORMAL));
    if (r <= 32) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Permission, "Windows did not grant Administrator rights (UAC declined)")));
        return;
    }
    QCoreApplication::quit();
#else
    QString command = "env";
    for (const auto& e : env) command += " " + shQuote(e);
    command += " " + shQuote(exe);
    for (const auto& a : args) command += " " + shQuote(a);
    auto* proc = new QProcess(this);
#if defined(Q_OS_MACOS)
    // AppleScript's prompt is the system administrator dialog; the app keeps running
    // after osascript returns because it is backgrounded with its output detached.
    const QString script = "do shell script \"" + QString(command + " >/dev/null 2>&1 &").replace("\\", "\\\\").replace("\"", "\\\"") + "\" with administrator privileges";
    proc->setProgram("osascript");
    proc->setArguments({"-e", script});
#else
    // pkexec drops the display variables; hand them back so the window can open.
    const auto pe = QProcessEnvironment::systemEnvironment();
    QStringList pk{"env"};
    for (const char* key : {"DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE", "QT_QPA_PLATFORM"})
        if (pe.contains(key)) pk << QString(key) + "=" + pe.value(key);
    pk << env << exe << args;
    proc->setProgram("pkexec");
    proc->setArguments(QStringList{"sh", "-c", "exec \"$@\" >/dev/null 2>&1 &", "sh"} + pk);
#endif
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        if (code == 0) {
            QCoreApplication::quit();
            return;
        }
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Permission, "the system did not grant elevated rights (prompt cancelled or refused)")));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Io, "could not start the elevation prompt: " + ss(proc->errorString()))));
        proc->deleteLater();
    });
    proc->start();
#endif
}

bool Workspace::canOpenPrivacySettings() const { return QSysInfo::productType() == "macos"; }

void Workspace::openPrivacySettings() {
    if (!canOpenPrivacySettings()) return;
    // The documented deep link into System Settings; "Privacy_AllFiles" is the Full Disk Access pane.
    if (!QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles")))
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Io, "could not open System Settings; look for Privacy & Security > Full Disk Access")));
}

void Workspace::revealAppInFinder() {
    QString bundle = QCoreApplication::applicationDirPath();   // .../Dr Stein.app/Contents/MacOS
    const int at = bundle.indexOf(".app/");
    if (at > 0) bundle = bundle.left(at + 4);
    else bundle = QCoreApplication::applicationFilePath();
    QProcess::startDetached("open", {"-R", bundle});
}

QVariantMap Workspace::sourceInfo(const QString& id) const {
    QVariantMap m;
    const auto* d = m_sources.find(ss(id));
    if (!d) return m;
    m["id"] = qs(d->id);
    m["name"] = qs(d->name);
    m["title"] = qs(d->title);
    m["path"] = qs(d->path);
    m["subtitle"] = qs(d->subtitle);
    m["identityLine"] = qs(d->identityLine);
    m["sizeText"] = qs(core::sizeText(d->sizeBytes));
    m["sizeBytes"] = static_cast<qulonglong>(d->sizeBytes);
    m["kind"] = d->kind == core::SourceKind::Disk ? "disk" : "image";
    m["kernelName"] = d->disk ? qs(d->disk->kernelName) : QString();
    m["serial"] = d->disk ? qs(d->disk->serial) : QString();
    m["removable"] = d->disk ? d->disk->removable : false;
    m["encrypted"] = d->image ? d->image->encrypted : false;
    m["locked"] = d->image ? d->image->locked : false;
    m["format"] = d->image ? qs(d->image->formatName) : QString();
    return m;
}

} // namespace drstein::ui
