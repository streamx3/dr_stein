// SPDX-License-Identifier: MIT
#include "image_controller.hpp"

#include "drstein/core/format.hpp"
#include "job_runner.hpp"
#include "stein/block/slice_device.hpp"
#include "settings.hpp"
#include "util.hpp"
#include "workspace.hpp"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace drstein::ui {

ImageController::ImageController(QObject* parent) : QObject(parent) {
    connect(Workspace::instance(), &Workspace::currentChanged, this, &ImageController::onSourceChanged);
    connect(Workspace::instance()->sources(), &SourceListModel::countChanged, this, [this] { Q_EMIT sourceChanged(); });
    onSourceChanged();
}

void ImageController::setMode(const QString& m) {
    if (m == m_mode) return;
    m_mode = m;
    if (m == "keys") reloadKeys();
    Workspace::instance()->setRestoreTarget(m == "restore" ? m_restoreTargetId.section('#', 0, 0) : QString());
    Q_EMIT modeChanged();
}

// ---- source ---------------------------------------------------------------------------

bool ImageController::hasSource() const { return Workspace::instance()->current() != nullptr; }
bool ImageController::sourceIsImage() const { return Workspace::instance()->isImage(); }
bool ImageController::sourceIsStein() const {
    const auto* c = Workspace::instance()->current();
    return c && c->descriptor.image && c->descriptor.image->format == stein::image::VdiskFormat::Stein;
}
bool ImageController::sourceEncrypted() const {
    const auto* c = Workspace::instance()->current();
    return c && c->descriptor.image && c->descriptor.image->encrypted;
}
QString ImageController::sourceTitle() const { return Workspace::instance()->title(); }
QString ImageController::sourcePath() const { return Workspace::instance()->path(); }
QString ImageController::sourceSizeText() const { return Workspace::instance()->sizeText(); }
QString ImageController::usedBlocksNote() const {
    const auto* c = Workspace::instance()->current();
    return c ? qs(core::usedBlocksNote(c->tree)) : QString();
}

QVariantList ImageController::sourceScopes() const {
    QVariantList out;
    const auto* c = Workspace::instance()->current();
    if (!c) return out;
    for (const auto& sc : core::sourceScopes(*c)) {
        QVariantMap m;
        m["label"] = qs(sc.label);
        m["partition"] = sc.partition;
        m["path"] = pathToVariant(sc.path);
        out.push_back(m);
    }
    return out;
}

void ImageController::setSourceScope(int i) {
    const auto* c = Workspace::instance()->current();
    if (!c) return;
    const auto scopes = core::sourceScopes(*c);
    if (i < 0 || static_cast<std::size_t>(i) >= scopes.size()) i = 0;
    m_scopeIndex = i;
    m_form.sourcePath = scopes[static_cast<std::size_t>(i)].path;
    // The default file name follows the scope: disk-2026-10-07.stein / disk-p2-2026-10-07.stein.
    if (m_form.destination.empty() || QFileInfo(pathText(m_form.destination)).fileName().contains(QRegularExpression("-(p\\d+-)?\\d{4}-\\d{2}-\\d{2}\\.(stein|img)$")))
        m_form.destination = ss(defaultDestination());
    revalidate();
}

void ImageController::onSourceChanged() {
    m_scopeIndex = 0;
    m_form.sourcePath.clear();
    if (m_form.destination.empty() || m_form.destination.filename().string().starts_with("dr-stein-")) m_form.destination = ss(defaultDestination());
    Workspace* w = Workspace::instance();
    if (w->isImage() && w->current()) {
        if (m_restore.image.empty() || m_restore.image != w->current()->descriptor.path) {
            m_restore.image = w->current()->descriptor.path;
            m_restore.passphrase.clear();
            reloadRestoreScope();
            m_restoreTargetId.clear();
            w->setRestoreTarget({});
        }
        if (sourceIsStein() && m_mode == "create") m_mode = "verify";
        if (!sourceIsStein() && (m_mode == "verify" || m_mode == "keys")) m_mode = "create";
    } else if (w->isDisk()) {
        if (m_mode == "verify" || m_mode == "keys") m_mode = "create";
    }
    Q_EMIT modeChanged();
    Q_EMIT sourceChanged();
    revalidate();
    if (m_mode == "keys") reloadKeys();
}

QString ImageController::defaultDestination() const {
    const Workspace* w = Workspace::instance();
    QString name = w->hasCurrent() ? w->title() : QStringLiteral("disk");
    name = name.toLower().replace(QRegularExpression("[^a-z0-9]+"), "-").remove(QRegularExpression("^-+|-+$"));
    if (name.isEmpty()) name = "disk";
    if (!m_form.sourcePath.empty() && w->current())
        if (const auto* node = core::nodeAt(w->current()->tree, m_form.sourcePath); node && node->partition) name += "-p" + QString::number(node->partition->index);
    const QString dir = Settings::instance()->lastImageDir();
    return QDir(dir).filePath(name + "-" + QDate::currentDate().toString("yyyy-MM-dd") + (m_form.raw ? ".img" : ".stein"));
}

// ---- create form ------------------------------------------------------------------------

void ImageController::revalidate() {
    m_plan.reset();
    m_validation.clear();
    m_warnings.clear();
    const auto* c = Workspace::instance()->current();
    if (!c) {
        m_validation = "Select a disk or an image to take an image of.";
    } else {
        auto plan = core::validate(m_form, *c);
        if (plan) {
            m_plan = std::move(*plan);
            for (const auto& w : m_plan->warnings) m_warnings << qs(w);
        } else {
            m_validation = qs(plan.error().message());
        }
    }
    Q_EMIT formChanged();
}

void ImageController::setDestination(const QString& d) {
    m_form.destination = ss(d);
    revalidate();
}
void ImageController::setDestinationUrl(const QUrl& url) {
    setDestination(url.toLocalFile());
    Settings::instance()->setLastImageDir(QFileInfo(url.toLocalFile()).absolutePath());
}
void ImageController::setRaw(bool on) {
    m_form.raw = on;
    revalidate();
}
QString ImageController::compression() const { return m_form.compression == stein::image::Compression::Lz4 ? "lz4" : "none"; }
void ImageController::setCompression(const QString& c) {
    m_form.compression = c == "none" ? stein::image::Compression::None : stein::image::Compression::Lz4;
    revalidate();
}
void ImageController::setChunkSizeText(const QString& t) {
    m_form.chunkSizeText = ss(t);
    revalidate();
}
void ImageController::setSplitSizeText(const QString& t) {
    m_form.splitSizeText = ss(t);
    revalidate();
}
bool ImageController::zeroFillBadSectors() const { return m_form.badSectors == stein::image::BadSectorPolicy::SkipZero; }
void ImageController::setZeroFillBadSectors(bool on) {
    m_form.badSectors = on ? stein::image::BadSectorPolicy::SkipZero : stein::image::BadSectorPolicy::Fail;
    revalidate();
}
void ImageController::setUsedBlocksOnly(bool on) {
    m_form.usedBlocksOnly = on;
    revalidate();
}
void ImageController::setVerifyAfter(bool on) {
    m_form.verifyAfter = on;
    revalidate();
}
void ImageController::setEncrypt(bool on) {
    m_form.encrypt = on;
    revalidate();
}
void ImageController::setPassphrase(const QString& p) {
    m_form.passphrase = ss(p);
    revalidate();
}
void ImageController::setNotes(const QString& n) {
    m_form.notes = ss(n);
    revalidate();
}

QString ImageController::planText() const {
    if (!m_plan) return {};
    QStringList lines;
    lines << (m_plan->raw ? "raw image (plain copy)" : "stein image");
    lines << "destination " + pathText(m_plan->destination);
    if (!m_plan->raw) {
        lines << "compression " + qs(std::string(stein::image::toString(m_plan->options.compression)));
        lines << "chunk " + qs(core::sizeBinary(m_plan->options.chunkSize));
        if (m_plan->options.splitSize) lines << "split every " + qs(core::sizeBinary(m_plan->options.splitSize));
        lines << (m_plan->options.usedBlocksOnly ? "used blocks only: " + usedBlocksNote() : "every byte stored");
        lines << (m_plan->options.passphrase.empty() ? "not encrypted" : "encrypted: ChaCha20-Poly1305 · " + qs(m_plan->options.kdf.describe()));
        lines << (m_plan->verifyAfter ? "verify after writing (level 3)" : "no verification afterwards");
    }
    lines << QString("bad sectors: ") + (m_plan->options.badSectors == stein::image::BadSectorPolicy::SkipZero ? "zero-fill and count" : "fail");
    return lines.join("\n");
}

void ImageController::start() {
    const auto* c = Workspace::instance()->current();
    if (!c || !m_plan) return;
    const core::CreatePlan plan = *m_plan;
    const core::OpenedSource* source = c;
    const bool started = JobRunner::instance()->start(
        QString(plan.raw ? "Copying " : "Imaging ") + qs(c->descriptor.title), "image",
        [source, plan](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            auto out = core::runCreate(*source, plan, progress, report);
            if (!out) return stein::fail(out.error());
            QString summary;
            if (plan.raw) summary = qs(core::sizeText(out->rawStats.bytesRead)) + " copied";
            else {
                summary = qs(core::sizeText(out->result.stats.bytesRead)) + " read · " + qs(core::sizeText(out->result.storedBytes)) + " stored";
                if (out->verified) summary += out->verify.chunksBad == 0 && out->verify.complete ? " · verified" : " · VERIFY FAILED";
            }
            JobRunner::instance()->setSummary(summary);
            return {};
        },
        [plan](bool ok, const stein::Error&) {
            // The new image joins the sidebar; the report stays on screen.
            if (ok) Workspace::instance()->addImagePath(pathText(plan.destination));
        });
    (void)started;
}

// ---- restore ------------------------------------------------------------------------------

void ImageController::reloadRestoreScope() {
    m_restoreScope.reset();
    m_restoreScopeError.clear();
    if (m_restore.image.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(m_restore.image, ec)) return;
    auto scope = core::restoreScopeOf(m_restore.image, m_restore.passphrase);
    if (scope) m_restoreScope = *scope;
    else m_restoreScopeError = qs(scope.error().message());
}

QString ImageController::restoreScopeText() const {
    if (!m_restoreScopeError.isEmpty()) return m_restoreScopeError;
    if (!m_restoreScope) return {};
    if (m_restoreScope->partition && m_restoreScope->provenance) return "partition image \u00b7 " + qs(m_restoreScope->provenance->text()) + " \u00b7 " + qs(core::sizeText(m_restoreScope->size));
    return "whole-device image \u00b7 " + qs(core::sizeText(m_restoreScope->size));
}

namespace {

QString padRight(QString text, int width) {
    while (text.size() < width) text += ' ';
    return text;
}

} // namespace

QVariantList ImageController::restoreTargets() const {
    QVariantList out;
    Workspace* w = Workspace::instance();
    const QString platformName = w->platformName();
    QList<QVariantMap> rows;
    if (m_restoreScope && m_restoreScope->partition) {
        // Partition images go into a partition of the source that is open: its tree is already probed.
        const auto* c = w->current();
        if (!c) return out;
        for (const auto& sc : core::sourceScopes(*c)) {
            if (!sc.partition) continue;
            const auto* node = core::nodeAt(c->tree, sc.path);
            if (!node || !node->partition) continue;
            QVariantMap m;
            m["id"] = qs(c->descriptor.id) + "#" + QString::number(node->partition->index);
            m["device"] = c->descriptor.disk ? qs(core::partitionOsPath(*c->descriptor.disk, node->partition->index, ss(platformName))) : QString("file #%1").arg(node->partition->index);
            if (m["device"].toString().isEmpty()) m["device"] = QString("partition %1").arg(node->partition->index);
            m["name"] = qs(sc.label) + " of " + qs(c->descriptor.title);
            m["subtitle"] = qs(c->descriptor.subtitle);
            m["path"] = qs(c->descriptor.path);
            m["sizeText"] = qs(core::sizeBinaryOrDecimal(node->region.length));
            m["sizeBytes"] = static_cast<qulonglong>(node->region.length);
            m["removable"] = c->descriptor.disk ? c->descriptor.disk->removable : false;
            m["isDisk"] = c->descriptor.kind == core::SourceKind::Disk;
            m["fits"] = node->region.length >= m_restoreScope->size;
            m["sameSize"] = node->region.length == m_restoreScope->size;
            rows.push_back(m);
        }
    } else {
        // Every disk and every raw image, the open one included; only the image being restored is out.
        const QString restoring = QString::fromStdString(m_restore.image.lexically_normal().string());
        for (const auto& s : w->sources()->items()) {
            if (!(s.kind == core::SourceKind::Disk || (s.image && s.image->format == stein::image::VdiskFormat::Raw && s.image->segments == 1))) continue;
            if (s.kind == core::SourceKind::ImageFile && qs(s.path) == restoring) continue;
            QVariantMap m;
            m["id"] = qs(s.id);
            m["device"] = s.kind == core::SourceKind::Disk ? qs(s.path) : QString("file");
            m["name"] = qs(s.kind == core::SourceKind::Disk ? s.title : s.name) + " \u00b7 " + qs(core::sizeText(s.sizeBytes));
            m["subtitle"] = qs(s.subtitle);
            m["path"] = qs(s.path);
            m["sizeText"] = qs(core::sizeText(s.sizeBytes));
            m["sizeBytes"] = static_cast<qulonglong>(s.sizeBytes);
            m["removable"] = s.disk ? s.disk->removable : false;
            m["isDisk"] = s.kind == core::SourceKind::Disk;
            m["fits"] = !m_restoreScope || s.sizeBytes >= m_restoreScope->size;
            m["sameSize"] = m_restoreScope && s.sizeBytes == m_restoreScope->size;
            rows.push_back(m);
        }
    }
    // The device column is padded to one width so the list reads as a table in a mono font.
    int width = 0;
    for (const auto& m : rows) width = std::max(width, static_cast<int>(m["device"].toString().size()));
    for (auto m : rows) {
        m["devicePadded"] = padRight(m["device"].toString(), width);
        m["fitText"] = m["sameSize"].toBool() ? "same size" : m["fits"].toBool() ? "larger" : "too small";
        out.push_back(m);
    }
    return out;
}

void ImageController::setRestoreTargetId(const QString& id) {
    if (id == m_restoreTargetId) return;
    m_restoreTargetId = id;
    // The sidebar marks the target in red for as long as it is the target.
    Workspace::instance()->setRestoreTarget(m_mode == "restore" ? id.section('#', 0, 0) : QString());
    Q_EMIT formChanged();
}

bool ImageController::checkRestorePassphrase(const QString& passphrase) {
    auto ok = core::passphraseOpens(m_restore.image, ss(passphrase));
    if (!ok) {
        Workspace::instance()->reportError(ok.error());
        return false;
    }
    if (!*ok) return false;
    m_restore.passphrase = ss(passphrase);
    reloadRestoreScope();   // the manifest (and a partition image's provenance) is readable now
    Q_EMIT formChanged();
    return true;
}

QVariantMap ImageController::restoreTarget() const {
    const QString sourceId = m_restoreTargetId.section('#', 0, 0);
    QVariantMap m = Workspace::instance()->sourceInfo(sourceId);
    if (m.isEmpty()) return m;
    for (const auto& t : restoreTargets())
        if (t.toMap()["id"].toString() == m_restoreTargetId) {
            m["targetName"] = t.toMap()["name"];
            m["targetSizeText"] = t.toMap()["sizeText"];
            m["isPartition"] = m_restoreTargetId.contains('#');
            m["partitionIndex"] = m_restoreTargetId.section('#', 1, 1).toInt();
        }
    return m;
}

void ImageController::setRestoreVerifyFirst(bool on) {
    m_restore.verifyFirst = on;
    Q_EMIT formChanged();
}
void ImageController::setRestoreAllowSmaller(bool on) {
    m_restore.allowSmaller = on;
    Q_EMIT formChanged();
}
void ImageController::setRestoreImagePath(const QString& p) {
    m_restore.image = ss(p);
    m_restore.passphrase.clear();
    reloadRestoreScope();
    setRestoreTargetId({});   // the target list changes with the image's scope
    Q_EMIT formChanged();
}
void ImageController::setRestoreImageUrl(const QUrl& url) {
    // A file picked here is a source like any other: it joins the sidebar and opens.
    const QString path = url.toLocalFile();
    Workspace::instance()->openImagePath(path);
    setRestoreImagePath(path);
}

bool ImageController::canRestore() const { return restoreMessage().isEmpty(); }

QString ImageController::restoreMessage() const {
    if (m_restore.image.empty()) return "Choose an image to restore.";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(m_restore.image, ec)) return "The image file does not exist.";
    if (!m_restoreScopeError.isEmpty()) return m_restoreScopeError;
    const bool partitionImage = m_restoreScope && m_restoreScope->partition;
    if (partitionImage && !Workspace::instance()->current()) return "Open the disk that holds the target partition.";
    const auto* t = Workspace::instance()->descriptor(ss(m_restoreTargetId.section('#', 0, 0)));
    if (!t) return partitionImage ? "Choose the target partition." : "Choose a target disk.";
    if (partitionImage != m_restoreTargetId.contains('#')) return partitionImage ? "A partition image goes into a partition." : "A whole-device image goes onto a whole device.";
    if (t->kind == core::SourceKind::Disk && !Workspace::instance()->elevated()) return "Restoring to a disk needs elevation.";
    for (const auto& x : restoreTargets())
        if (x.toMap()["id"].toString() == m_restoreTargetId && !x.toMap()["fits"].toBool() && !m_restore.allowSmaller) return "The target is smaller than the image.";
    return {};
}

void ImageController::restore(const QString& passphrase) {
    if (!canRestore()) return;
    const auto* t = Workspace::instance()->descriptor(ss(m_restoreTargetId.section('#', 0, 0)));
    if (!t) return;
    core::RestoreForm form = m_restore;
    form.passphrase = ss(passphrase);
    const core::SourceDescriptor target = *t;
    // A partition target is a slice of the device, found by index in the probed tree.
    std::optional<stein::Region> region;
    QString what = qs(target.title);
    if (m_restoreTargetId.contains('#')) {
        const auto index = m_restoreTargetId.section('#', 1, 1).toUInt();
        const auto* c = Workspace::instance()->current();
        if (!c) return;
        for (const auto& child : c->tree.children)
            if (child.partition && child.partition->index == index) region = child.region;
        if (!region) return;
        what += " \u00b7 partition " + QString::number(index);
    }
    const bool started = JobRunner::instance()->start(
        "Restoring to " + what, "image",
        [form, target, region](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            core::OpenOptions o;
            o.writable = true;
            auto dev = core::openDevice(target, o);
            if (!dev) return stein::fail(dev.error());
            std::shared_ptr<stein::BlockDevice> device = *dev;
            if (region) {
                auto slice = stein::SliceDevice::create(device, *region, target.name + " partition");
                if (!slice) return stein::fail(slice.error());
                device = *slice;
            }
            auto r = core::runRestore(form, *device, progress, report);
            if (!r) return stein::fail(r.error());
            QString summary = qs(core::sizeText(r->stats.bytesRead)) + " restored";
            if (r->targetLarger) summary += " · target is larger";
            if (r->targetSmaller) summary += " · target was smaller: truncated";
            JobRunner::instance()->setSummary(summary);
            return {};
        },
        [target](bool ok, const stein::Error&) {
            Workspace::instance()->setRestoreTarget({});
            if (ok && Workspace::instance()->currentId() == qs(target.id)) Workspace::instance()->reprobe();
        });
    (void)started;
}

// ---- verify -------------------------------------------------------------------------------

void ImageController::setVerifyLevel(int level) {
    m_verifyLevel = std::clamp(level, 1, 3);
    Q_EMIT formChanged();
}

bool ImageController::canVerify() const { return sourceIsStein(); }

void ImageController::verify(const QString& passphrase) {
    const auto* c = Workspace::instance()->current();
    if (!c || !sourceIsStein()) return;
    core::VerifyForm form;
    form.image = c->descriptor.path;
    form.level = m_verifyLevel;
    form.passphrase = ss(passphrase);
    if (form.passphrase.empty()) {
        const auto list = Workspace::instance()->passphrasesFor(c->descriptor.id);
        if (!list.empty()) form.passphrase = list.front();
    }
    JobRunner::instance()->start(
        "Verifying " + qs(c->descriptor.name) + " (level " + QString::number(m_verifyLevel) + ")", "image",
        [form](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            auto v = core::runVerify(form, progress, report);
            if (!v) return stein::fail(v.error());
            JobRunner::instance()->setSummary(qs(core::verifyText(*v)));
            return {};
        });
}

// ---- keys ---------------------------------------------------------------------------------

void ImageController::reloadKeys() {
    m_keys.clear();
    m_keysMessage.clear();
    const auto* c = Workspace::instance()->current();
    if (!c || !sourceIsStein()) {
        m_keysMessage = "Key slots belong to encrypted .stein images.";
    } else if (!sourceEncrypted()) {
        m_keysMessage = "This image is not encrypted.";
    } else {
        auto keys = core::listKeys(c->descriptor.path);
        if (!keys) m_keysMessage = qs(keys.error().message());
        else
            for (const auto& k : *keys) {
                QVariantMap m;
                m["id"] = k.id;
                m["label"] = qs(k.label);
                m["kdf"] = qs(k.kdfText);
                m_keys.push_back(m);
            }
    }
    Q_EMIT keysChanged();
}

QVariantMap ImageController::addKey(const QString& current, const QString& fresh, const QString& label) {
    const auto* c = Workspace::instance()->current();
    if (!c) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no image"));
    auto r = core::addKey(c->descriptor.path, ss(current), ss(fresh), ss(label));
    if (!r) return errorToVariant(r.error());
    reloadKeys();
    return {};
}

QVariantMap ImageController::removeKey(const QString& current, int id) {
    const auto* c = Workspace::instance()->current();
    if (!c) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no image"));
    auto r = core::removeKey(c->descriptor.path, ss(current), id);
    if (!r) return errorToVariant(r.error());
    reloadKeys();
    return {};
}

} // namespace drstein::ui
