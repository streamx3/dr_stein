// SPDX-License-Identifier: MIT
#include "workspace.hpp"

#include "drstein/core/format.hpp"
#include "job_runner.hpp"
#include "settings.hpp"
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

#include <cerrno>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace drstein::ui {

namespace {
Workspace* g_instance = nullptr;
}

Workspace::Workspace(QObject* parent) : QObject(parent) {
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
    std::vector<core::SourceDescriptor> items;
    auto disks = core::listDisks(stein::platform::current());
    if (disks) items = std::move(*disks);
    else Q_EMIT error(errorToVariant(disks.error()));
    for (const auto& img : m_images) items.push_back(img);
    m_sources.setItems(std::move(items));
    m_sources.setSelected(m_selectedSourceId);
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
    if (!m_current || !m_current->descriptor.disk) return;
    const QString mp = m_details.mountpoint();
    if (mp.isEmpty()) return;
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

void Workspace::openImage(const QUrl& url) { openImagePath(url.isLocalFile() ? url.toLocalFile() : url.toString()); }

void Workspace::addImagePath(const QString& path) {
    const std::string id = "image:" + ss(QFileInfo(path).absoluteFilePath());
    const auto known = passphrasesFor(id);
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
    if (m_currentId == id) setCurrent(std::nullopt, {}, {});
}

std::vector<std::string> Workspace::passphrasesFor(const std::string& id) const {
    auto it = m_passphrases.find(id);
    return it == m_passphrases.end() ? std::vector<std::string>{} : it->second;
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
    options.passphrases = passphrasesFor(id);
    auto result = std::make_shared<stein::Expected<core::OpenedSource>>(stein::fail(stein::ErrorCategory::Internal, "not run"));
    setBusy(true);
    const bool started = JobRunner::instance()->start(
        "Probing " + qs(desc.name), "probe",
        [desc, options, result](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            progress.setPhase("Probing", 0, "steps");
            report.start();
            *result = core::openSource(desc, options);
            if (!*result) {
                report.addLine(result->error().toString());
                report.finish(stein::ReportStatus::Error);
                return stein::fail(result->error());
            }
            report.addDetail("device", (*result)->device->name());
            report.finish(stein::ReportStatus::Success);
            return {};
        },
        [this, id, result, silentOnError](bool ok, const stein::Error& err) {
            setBusy(false);
            if (ok) setCurrent(std::move(**result), {}, id);
            else {
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
                if (silentOnError) return;
                if (err.category() == stein::ErrorCategory::Permission && m_sources.find(id) && m_sources.find(id)->image && m_sources.find(id)->image->encrypted)
                    Q_EMIT passphraseNeeded(qs(m_sources.find(id)->name));
            }
        });
    if (!started) setBusy(false);
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
    if (!m_current) {
        m_details.set({}, false);
        return;
    }
    if (m_selectedMetadata >= 0) m_details.set(core::metadataDetails(m_current->tree, m_selectedMetadata), true);
    else {
        core::NodeDetails d = core::nodeDetails(m_current->tree, m_selectedPath);
        if (m_current->descriptor.disk) core::annotateOsDevice(d, m_current->tree, m_selectedPath, *m_current->descriptor.disk, stein::platform::current().name(), m_osMounts);
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

void Workspace::unlock(const QString& passphrase) {
    const std::string id = m_currentId.empty() ? m_selectedSourceId : m_currentId;
    if (id.empty() || passphrase.isEmpty()) return;
    auto& list = m_passphrases[id];
    const std::string p = ss(passphrase);
    bool known = false;
    for (const auto& x : list)
        if (x == p) known = true;
    if (!known) list.push_back(p);
    // Images that were locked need a fresh describe (manifest, hash) before the open.
    if (const auto* d = m_sources.find(id); d && d->kind == core::SourceKind::ImageFile)
        if (auto fresh = core::describeImageFile(d->image->path, p)) {
            m_sources.replace(*fresh);
            for (auto& img : m_images)
                if (img.id == id) img = *fresh;
        }
    openSourceAsync(id, false);
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
    if (const QByteArray view = qgetenv("DRSTEIN_VIEW"); !view.isEmpty()) env << "DRSTEIN_VIEW=" + QString::fromUtf8(view);
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
