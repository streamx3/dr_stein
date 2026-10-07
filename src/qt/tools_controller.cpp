// SPDX-License-Identifier: MIT
#include "tools_controller.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/opened.hpp"
#include "drstein/core/topology.hpp"
#include "job_runner.hpp"
#include "util.hpp"
#include "workspace.hpp"

#include <QFile>
#include <QTextStream>

#include <mutex>

namespace drstein::ui {

ToolsController::ToolsController(QObject* parent) : QObject(parent) {
    connect(Workspace::instance(), &Workspace::currentChanged, this, [this] {
        const auto* c = Workspace::instance()->current();
        const std::string id = c ? c->descriptor.id : std::string{};
        if (id != m_sourceId) {
            m_sourceId = id;
            resetResults();
        }
        Q_EMIT changed();
    });
    resetResults();
}

void ToolsController::resetResults() {
    m_scan.reset();
    m_test.reset();
    m_cells.clear();
    for (int i = 0; i < cellCount(); ++i) m_cells.push_back(0);
    Q_EMIT scanChanged();
    Q_EMIT testChanged();
}

bool ToolsController::hasSource() const { return Workspace::instance()->current() != nullptr; }
QString ToolsController::targetTitle() const { return Workspace::instance()->title(); }
QString ToolsController::targetPath() const { return Workspace::instance()->path(); }

QString ToolsController::targetIdentity() const {
    const auto* c = Workspace::instance()->current();
    if (!c) return {};
    QString s = qs(c->descriptor.title) + " · " + qs(core::sizeText(c->device->size()));
    if (c->descriptor.disk) {
        s += " · " + qs(core::busName(c->descriptor.disk->bus));
        if (c->descriptor.disk->removable) s += " · removable";
    } else s += " · image file";
    return s;
}

QString ToolsController::targetContents() const {
    const auto* c = Workspace::instance()->current();
    if (!c) return {};
    QStringList parts;
    if (c->tree.table && c->tree.table->type() != stein::pt::TableType::None) parts << qs(core::schemeName(c->tree.table->type())) + " · " + QString::number(c->tree.table->partitions().size()) + (c->tree.table->partitions().size() == 1 ? " partition" : " partitions");
    for (const auto& child : c->tree.children)
        if (child.content) parts << qs(core::contentSummary(child));
    if (c->tree.content) parts << qs(core::contentSummary(c->tree));
    return parts.isEmpty() ? "nothing recognised" : parts.join(" · ");
}

QString ToolsController::expectedConfirm() const {
    const auto* c = Workspace::instance()->current();
    if (!c || !c->descriptor.disk) return {};
    return qs(c->descriptor.disk->kernelName.empty() ? c->descriptor.path : c->descriptor.disk->kernelName);
}

bool ToolsController::isRealDevice() const {
    const auto* c = Workspace::instance()->current();
    return c && c->descriptor.kind == core::SourceKind::Disk;
}

bool ToolsController::canScan() const { return hasSource() && !JobRunner::instance()->running(); }

QString ToolsController::testBlockedReason() const {
    const Workspace* w = Workspace::instance();
    const auto* c = w->current();
    if (!c) return "select a device";
    if (c->descriptor.kind == core::SourceKind::Disk && !w->elevated()) return "needs elevation";
    if (c->descriptor.image && (c->descriptor.image->format != stein::image::VdiskFormat::Raw || c->descriptor.image->segments > 1)) return "containers are read-only";
    if (c->descriptor.disk) {
        auto mounts = stein::platform::current().mounts(c->descriptor.path);
        if (mounts && !mounts->empty()) return "refuses while any volume on " + qs(c->descriptor.path) + " is mounted";
    }
    return {};
}

bool ToolsController::canTest() const { return testBlockedReason().isEmpty() && !JobRunner::instance()->running(); }

QStringList ToolsController::badLines() const {
    QStringList out;
    if (m_scan)
        for (const auto& l : m_scan->badLines) out << qs(l);
    return out;
}

void ToolsController::updateCells(stein::ByteCount tested, const std::vector<stein::Region>& bad) {
    const auto* c = Workspace::instance()->current();
    if (!c) return;
    const auto cells = core::scanCells(c->device->size(), bad, static_cast<std::size_t>(cellCount()), tested);
    m_cells.clear();
    for (auto s : cells) m_cells.push_back(static_cast<int>(s));
    Q_EMIT scanChanged();
}

void ToolsController::surfaceScan() {
    const auto* c = Workspace::instance()->current();
    if (!c || !canScan()) return;
    m_scan.reset();
    updateCells(0, {});
    const core::OpenedSource* source = c;
    auto result = std::make_shared<std::optional<stein::ops::SurfaceScanResult>>();
    auto reportText = std::make_shared<std::string>();
    // Live cells follow the progress snapshot.
    QObject* ctx = new QObject(this);
    connect(JobRunner::instance(), &JobRunner::progressChanged, ctx, [this] {
        if (!JobRunner::instance()->running()) return;
        const auto fraction = JobRunner::instance()->fraction();
        const auto* cur = Workspace::instance()->current();
        if (cur) updateCells(static_cast<stein::ByteCount>(fraction * static_cast<double>(cur->device->size())), {});
    });
    JobRunner::instance()->start(
        "Scanning " + qs(c->descriptor.title), "tools",
        [source, result, reportText](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            auto r = core::runSurfaceScan(*source->device, progress, report);
            if (!r) return stein::fail(r.error());
            *result = *r;
            *reportText = report.toText();
            JobRunner::instance()->setSummary(r->healthy() ? "no unreadable sectors" : qs(std::to_string(r->unreadableSectors) + " unreadable sector(s)"));
            return {};
        },
        [this, ctx, result, reportText](bool ok, const stein::Error&) {
            ctx->deleteLater();
            const auto* cur = Workspace::instance()->current();
            if (ok && *result && cur) {
                m_scan = core::summarize(**result, cur->tree, cur->device->sectorSize());
                m_scanReportText = qs(*reportText);
                updateCells((*result)->bytesRead, (*result)->badRegions);
            }
            Q_EMIT scanChanged();
            Q_EMIT changed();
        });
}

QVariantMap ToolsController::capacityTest(const QString& confirmText, bool quick, bool keepPattern) {
    const auto* c = Workspace::instance()->current();
    if (!c) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no device"));
    if (const QString why = testBlockedReason(); !why.isEmpty()) return errorToVariant(stein::Error(stein::ErrorCategory::Busy, ss(why)));
    core::CapacityTestRequest req;
    req.confirmText = ss(confirmText);
    req.expectedConfirm = ss(expectedConfirm());
    req.quick = quick;
    req.keepPattern = keepPattern;
    if (!req.expectedConfirm.empty() && req.confirmText != req.expectedConfirm)
        return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "type " + req.expectedConfirm + " to confirm"));
    const core::SourceDescriptor target = c->descriptor;
    const std::vector<std::string> passphrases = Workspace::instance()->passphrasesFor(target.id);
    m_test.reset();
    auto result = std::make_shared<std::optional<stein::ops::CapacityTestResult>>();
    JobRunner::instance()->start(
        "Capacity test on " + qs(target.title), "tools",
        [target, passphrases, req, result](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            core::OpenOptions o;
            o.writable = true;
            o.passphrases = passphrases;
            auto dev = core::openDevice(target, o);
            if (!dev) return stein::fail(dev.error());
            auto r = core::runCapacityTest(**dev, req, progress, report);
            if (!r) return stein::fail(r.error());
            *result = *r;
            JobRunner::instance()->setSummary(qs(core::summarize(*r).verdict + ": " + core::summarize(*r).detail));
            return {};
        },
        [this, result](bool ok, const stein::Error&) {
            if (ok && *result) m_test = core::summarize(**result);
            Q_EMIT testChanged();
            Q_EMIT changed();
            Workspace::instance()->reprobe();
        });
    return {};
}

QString ToolsController::saveScanReport(const QUrl& file) {
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return "could not write " + file.toLocalFile();
    QTextStream out(&f);
    out << "Surface scan of " << targetTitle() << " (" << targetPath() << ")\n";
    out << "Scanned: " << scannedText() << "\nUnreadable: " << unreadableText() << "\nRead: " << scanRateText() << "\n";
    for (const QString& l : badLines()) out << l << "\n";
    out << "\n" << m_scanReportText;
    return {};
}

} // namespace drstein::ui
