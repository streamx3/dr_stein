// SPDX-License-Identifier: MIT
#include "partition_editor.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/progress.hpp"
#include "job_runner.hpp"
#include "util.hpp"
#include "workspace.hpp"

namespace drstein::ui {

PartitionEditor::PartitionEditor(QObject* parent) : QObject(parent) {
    connect(Workspace::instance(), &Workspace::currentChanged, this, [this] {
        if (!Workspace::instance()->current() || Workspace::instance()->current()->descriptor.id != m_sourceId) reload();
    });
    connect(Workspace::instance(), &Workspace::probed, this, [this] {
        // The device changed under us (hex write, restore, refresh, unmount): reopen unless edits are pending.
        if (!m_session || m_session->stack().empty()) reload();
    });
    connect(Workspace::instance(), &Workspace::mountsChanged, this, [this] {
        if (!m_session && Workspace::instance()->view() == "partitions") reload();
    });
    connect(Workspace::instance(), &Workspace::viewChanged, this, [this] {
        // The writable handle lives only while the view shows; pending edits keep it.
        const bool showing = Workspace::instance()->view() == "partitions";
        if (showing && !m_session) reload();
        else if (!showing && m_session && m_session->stack().empty()) reload();
    });
    reload();
}

void PartitionEditor::reload() {
    m_session.reset();
    m_reason.clear();
    m_sourceId.clear();
    const Workspace* w = Workspace::instance();
    const auto* c = w->current();
    if (w->view() != "partitions") {
        m_reason = "Open a disk or a raw image to edit its partition table.";
    } else if (!c) {
        m_reason = "Open a disk or a raw image to edit its partition table.";
    } else if (c->descriptor.kind == core::SourceKind::Disk && !w->elevated()) {
        m_reason = "Editing a disk's partition table needs elevation. Image files can be edited without it.";
    } else if (c->descriptor.image && (c->descriptor.image->format != stein::image::VdiskFormat::Raw || c->descriptor.image->segments > 1)) {
        m_reason = qs(c->descriptor.image->formatName) + " containers open read-only; restore the image to a raw file or a disk to edit it.";
    } else {
        m_sourceId = c->descriptor.id;
        core::OpenOptions o;
        o.passphrases = w->passphrasesFor(m_sourceId);
        auto s = core::EditSession::open(c->descriptor, o);
        if (s) m_session.emplace(std::move(*s));
        else {
            const auto pres = core::present(s.error());
            m_reason = qs(pres.message) + (pres.hint.empty() ? "" : "\n\n" + qs(pres.hint));
        }
    }
    Q_EMIT changed();
}

bool PartitionEditor::hasTable() const { return m_session && m_session->preview().table && m_session->preview().table->type() != stein::pt::TableType::None; }

QString PartitionEditor::scheme() const {
    if (!m_session || !m_session->preview().table) return {};
    return qs(core::schemeName(m_session->preview().table->type()));
}

QString PartitionEditor::summary() const { return m_session ? qs(core::stackSummary(m_session->stack())) : QString(); }
QString PartitionEditor::changedRanges() const { return m_session ? qs(core::changedRangesText(m_session->stack())) : QString(); }
int PartitionEditor::pendingCount() const { return m_session ? static_cast<int>(m_session->stack().pending().size()) : 0; }
bool PartitionEditor::destructive() const { return m_session && m_session->stack().destructive(); }

QVariantList PartitionEditor::pending() const {
    QVariantList out;
    if (!m_session) return out;
    for (const auto& r : core::pendingRows(m_session->stack())) {
        QVariantMap m;
        m["n"] = r.n;
        m["title"] = qs(r.title);
        m["detail"] = qs(r.detail);
        m["destructive"] = r.destructive;
        out.push_back(m);
    }
    return out;
}

QVariantList PartitionEditor::previewRows() const {
    QVariantList out;
    if (!m_session) return out;
    for (const auto& r : core::previewRows(m_session->base(), m_session->preview())) {
        QVariantMap m;
        m["path"] = pathToVariant(r.path);
        m["name"] = qs(r.name);
        m["typeCode"] = qs(r.typeCode);
        m["sizeText"] = qs(r.sizeText);
        m["change"] = qs(r.change);
        m["colorIndex"] = r.colorIndex;
        m["index"] = r.index;
        m["isFree"] = r.isFree;
        m["changed"] = r.changed;
        m["deleted"] = r.deleted;
        out.push_back(m);
    }
    return out;
}

QVariantList PartitionEditor::segmentsToVariant(const std::vector<core::Segment>& segs) const {
    QVariantList out;
    for (const auto& s : segs) {
        QVariantMap m;
        m["offset"] = static_cast<qulonglong>(s.region.offset);
        m["length"] = static_cast<qulonglong>(s.region.length);
        m["label"] = qs(s.label);
        m["isFree"] = s.isFree;
        m["colorIndex"] = s.colorIndex;
        out.push_back(m);
    }
    return out;
}

QVariantList PartitionEditor::baseSegments() const { return m_session ? segmentsToVariant(core::baseSegments(m_session->base())) : QVariantList{}; }
QVariantList PartitionEditor::previewSegments() const {
    return m_session ? segmentsToVariant(core::previewSegments(m_session->base(), m_session->preview())) : QVariantList{};
}

QVariantList PartitionEditor::partitions() const {
    QVariantList out;
    if (!m_session || !m_session->preview().table) return out;
    const auto& table = *m_session->preview().table;
    const std::uint32_t ss = table.geometry().logicalSectorSize;
    for (const auto& p : table.partitions()) {
        QVariantMap m;
        m["index"] = p.index;
        m["name"] = qs(p.name);
        m["typeText"] = qs(core::partitionTypeText(p));
        const auto* info = stein::pt::types::find(p.type);
        m["typeCode"] = qs(p.type.scheme == stein::pt::TableType::Gpt && info && info->sgdiskCode[0] ? std::string(info->sgdiskCode) : p.type.code());
        m["startLba"] = static_cast<qulonglong>(p.firstLba);
        m["endLba"] = static_cast<qulonglong>(p.lastLba);
        m["sizeText"] = qs(core::sizeBinaryOrDecimal(p.sectors() * ss));
        m["sizeBytes"] = static_cast<qulonglong>(p.sectors() * ss);
        m["isExtended"] = p.isExtended;
        m["isLogical"] = p.isLogical;
        out.push_back(m);
    }
    return out;
}

QVariantList PartitionEditor::typeChoices() const {
    QVariantList out;
    if (!m_session || !m_session->preview().table) return out;
    for (const auto& c : core::partitionTypeChoices(m_session->preview().table->type())) {
        QVariantMap m;
        m["code"] = qs(c.code);
        m["name"] = qs(c.name);
        m["group"] = qs(c.group);
        m["label"] = qs(c.code) + " · " + qs(c.name);
        out.push_back(m);
    }
    return out;
}

bool PartitionEditor::isRealDevice() const { return m_session && m_session->isRealDevice(); }

QString PartitionEditor::deviceIdentity() const {
    const auto* c = Workspace::instance()->current();
    if (!c) return {};
    return qs(c->descriptor.title) + " · " + qs(c->descriptor.path) + " · " + qs(core::sizeText(c->device->size()));
}

QString PartitionEditor::freeSpaceHint() const {
    if (!m_session || !m_session->preview().table) return {};
    const auto& table = *m_session->preview().table;
    const std::uint32_t ss = table.geometry().logicalSectorSize;
    const auto free = table.freeRegions(std::max<stein::SectorCount>(1, stein::MiB / ss));
    if (free.empty()) return "no free space";
    stein::SectorCount largest = 0;
    for (const auto& f : free) largest = std::max(largest, f.sectors());
    return "largest free region " + qs(core::sizeText(largest * ss)) + " · " + QString::number(free.size()) + (free.size() == 1 ? " region" : " regions");
}

bool PartitionEditor::canRepair() const {
    if (!m_session || !m_session->preview().table) return false;
    for (const auto& d : m_session->preview().table->diagnostics())
        if (d.repairable) return true;
    return false;
}

QVariantMap PartitionEditor::report(const stein::Expected<void>& r) {
    Q_EMIT changed();
    if (!r) return errorToVariant(r.error());
    return {};
}

QVariantMap PartitionEditor::addPartition(const QString& start, const QString& size, const QString& end, const QString& type, const QString& name, bool wipe) {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    core::AddPartitionForm f;
    f.startText = ss(start);
    f.sizeText = ss(size);
    f.endText = ss(end);
    f.typeText = ss(type);
    f.name = ss(name);
    f.wipeSignatures = wipe;
    return report(m_session->addPartition(f));
}

QVariantMap PartitionEditor::editPartition(int index, const QString& start, const QString& size, const QString& end, const QString& type, const QString& name, bool nameChanged) {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    core::EditPartitionForm f;
    f.startText = ss(start);
    f.sizeText = ss(size);
    f.endText = ss(end);
    f.typeText = ss(type);
    if (nameChanged) f.name = ss(name);
    return report(m_session->updatePartition(static_cast<std::uint32_t>(index), f));
}

QVariantMap PartitionEditor::deletePartition(int index) {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    return report(m_session->deletePartition(static_cast<std::uint32_t>(index)));
}

QVariantMap PartitionEditor::repairTable() {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    return report(m_session->repairTable());
}

QVariantMap PartitionEditor::newTable(const QString& scheme) {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    const QString s = scheme.toLower();
    stein::pt::TableType t = s == "gpt" ? stein::pt::TableType::Gpt : s == "mbr" ? stein::pt::TableType::Mbr : s == "apm" ? stein::pt::TableType::Apm : stein::pt::TableType::Unknown;
    if (t == stein::pt::TableType::Unknown) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "unknown scheme " + ss(scheme)));
    return report(m_session->createTable(t));
}

QVariantMap PartitionEditor::wipe(int index) {
    if (!m_session) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no edit session"));
    return report(m_session->wipeSignatures(index > 0 ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(index)) : std::nullopt));
}

void PartitionEditor::undoLast() {
    if (!m_session) return;
    if (auto r = m_session->undoLast(); !r) Workspace::instance()->reportError(r.error());
    Q_EMIT changed();
}

void PartitionEditor::clear() {
    if (!m_session) return;
    m_session->clear();
    Q_EMIT changed();
}

void PartitionEditor::apply() {
    if (!m_session || m_session->stack().empty()) return;
    core::EditSession* session = &*m_session;
    const int count = pendingCount();
    JobRunner::instance()->start(
        "Applying " + QString::number(count) + (count == 1 ? " operation" : " operations"), "partitions",
        [session](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            report.start();
            auto r = session->apply(progress);
            if (!r) {
                report.addLine(r.error().toString());
                report.finish(stein::ReportStatus::Error);
                return stein::fail(r.error());
            }
            core::copyReportInto(report, r->report);
            report.addDetail("device matches preview", r->postconditionOk ? "yes" : "NO: re-probe");
            report.finish(r->postconditionOk ? stein::ReportStatus::Success : stein::ReportStatus::Warning);
            JobRunner::instance()->setSummary(r->postconditionOk ? "applied; the device matches the preview" : "applied, but the device does not match the preview");
            return {};
        },
        [this](bool ok, const stein::Error&) {
            Q_EMIT changed();
            Q_EMIT applied(ok);
            Workspace::instance()->reprobe();
        });
}

} // namespace drstein::ui
