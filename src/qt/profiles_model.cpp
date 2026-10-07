// SPDX-License-Identifier: MIT
#include "profiles_model.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/imaging.hpp"
#include "drstein/core/progress.hpp"
#include "job_runner.hpp"
#include "settings.hpp"
#include "util.hpp"
#include "workspace.hpp"

namespace drstein::ui {

namespace {
ProfilesModel* g_instance = nullptr;
}

ProfilesModel::ProfilesModel(QObject* parent) : QAbstractListModel(parent) {
    connect(Settings::instance(), &Settings::profilesDirChanged, this, &ProfilesModel::reload);
    connect(Workspace::instance()->sources(), &SourceListModel::countChanged, this, &ProfilesModel::reload);
    connect(Workspace::instance(), &Workspace::currentChanged, this, [this] { Q_EMIT countChanged(); });
    reload();
}

ProfilesModel* ProfilesModel::instance() {
    if (!g_instance) g_instance = new ProfilesModel();
    return g_instance;
}

ProfilesModel* ProfilesModel::create(QQmlEngine*, QJSEngine*) {
    ProfilesModel* p = instance();
    QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
    return p;
}

QString ProfilesModel::directory() const { return Settings::instance()->profilesDir(); }

bool ProfilesModel::canCreateFromCurrent() const {
    const auto* c = Workspace::instance()->current();
    return c && (c->descriptor.kind == core::SourceKind::Disk || (c->descriptor.image && c->descriptor.image->format == stein::image::VdiskFormat::Raw));
}

QString ProfilesModel::createHint() const {
    const auto* c = Workspace::instance()->current();
    if (!c) return "Select a disk first.";
    if (!canCreateFromCurrent()) return "Profiles target disks (or raw image files for testing).";
    return "New profile for " + qs(c->descriptor.title);
}

QVariant ProfilesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& c = m_cards[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Name: return qs(c.stored.profile.name);
    case Kicker: return qs(c.kicker);
    case Description: return qs(c.stored.profile.description);
    case DiskText: return qs(c.diskText);
    case DiskPresent: return c.diskPresent;
    case SelectorText: return qs(c.selectorText);
    case ImageText: return qs(c.imageText);
    case ImagePresent: return c.imagePresent;
    case MadeText: return qs(c.madeText);
    case FitsText: return qs(c.fitsText);
    case PolicyText: return qs(c.policyText);
    case VerifyLevelText: return qs(c.verifyLevelText);
    case CanBackup: return c.canBackup;
    case CanRestore: return c.canRestore;
    case CanVerify: return c.canVerify;
    case Warnings: {
        QStringList w;
        for (const auto& x : c.warnings) w << qs(x);
        return w;
    }
    case File: return pathText(c.stored.file);
    case ImagePath: return pathText(c.stored.profile.image.path);
    case Encrypted: return c.stored.profile.image.encrypt;
    case RequiresElevation: return c.stored.profile.policy.requireElevated;
    }
    return {};
}

QHash<int, QByteArray> ProfilesModel::roleNames() const {
    return {{Name, "name"}, {Kicker, "kicker"}, {Description, "description"}, {DiskText, "diskText"}, {DiskPresent, "diskPresent"},
            {SelectorText, "selectorText"}, {ImageText, "imageText"}, {ImagePresent, "imagePresent"}, {MadeText, "madeText"},
            {FitsText, "fitsText"}, {PolicyText, "policyText"}, {VerifyLevelText, "verifyLevelText"}, {CanBackup, "canBackup"},
            {CanRestore, "canRestore"}, {CanVerify, "canVerify"}, {Warnings, "warnings"}, {File, "file"}, {ImagePath, "imagePath"},
            {Encrypted, "encrypted"}, {RequiresElevation, "requiresElevation"}};
}

QVariantMap ProfilesModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

void ProfilesModel::reload() {
    core::ProfileStore store(ss(directory()));
    auto list = store.list();
    beginResetModel();
    m_cards.clear();
    if (list)
        for (const auto& s : *list) m_cards.push_back(core::profileCard(s));
    endResetModel();
    Q_EMIT countChanged();
}

void ProfilesModel::run(int row, const QString& scenarioName, bool dryRun, const QString& passphrase) {
    if (row < 0 || row >= rowCount()) return;
    const auto& card = m_cards[static_cast<std::size_t>(row)];
    core::Scenario scenario = scenarioName == "backup" ? core::Scenario::Backup : scenarioName == "restore" ? core::Scenario::Restore : core::Scenario::Verify;
    stein::app::RunOptions o;
    o.dryRun = dryRun;
    o.passphrase = ss(passphrase);
    o.unlock = card.stored.profile.target.osPath.size() && !card.stored.profile.target.hasIdentity();
    const stein::app::Profile profile = card.stored.profile;
    const QString title = QString(dryRun ? "Dry run: " : "") + qs(std::string(core::toString(scenario))) + " · " + qs(profile.name);
    JobRunner::instance()->start(
        title, "profile",
        [scenario, profile, o](stein::Progress& progress, stein::Report& report) -> stein::Expected<void> {
            auto r = core::runScenario(scenario, profile, o, progress);
            if (!r) {
                report.addLine(r.error().toString());
                report.finish(stein::ReportStatus::Error);
                return stein::fail(r.error());
            }
            // The scenario's own report becomes the card's report.
            core::copyReportInto(report, r->report);
            report.finish(r->ok ? stein::ReportStatus::Success : stein::ReportStatus::Error);
            QString summary;
            if (r->created) summary = qs(core::sizeText(r->created->stats.bytesRead)) + " backed up · " + qs(core::sizeText(r->created->storedBytes)) + " stored";
            else if (r->restored) summary = qs(core::sizeText(r->restored->stats.bytesRead)) + " restored";
            else if (r->verified) summary = qs(core::verifyText(*r->verified));
            if (o.dryRun) summary = "dry run: " + (summary.isEmpty() ? "checks passed" : summary);
            if (!r->ok) summary = "FAILED: see the report";
            JobRunner::instance()->setSummary(summary);
            if (!r->ok) return stein::fail(stein::ErrorCategory::Internal, "the scenario reported a failure; see the report");
            return {};
        },
        [this](bool, const stein::Error&) { reload(); });
}

QVariantMap ProfilesModel::newFromCurrent(const QUrl& imageFile, const QString& name, const QString& description, bool usedOnly, bool encrypt) {
    const auto* c = Workspace::instance()->current();
    if (!c || !canCreateFromCurrent()) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "select a disk first"));
    stein::app::Profile p = c->descriptor.disk ? core::profileFromDisk(*c->descriptor.disk, pathOf(imageFile), ss(name))
                                               : core::profileFromFile(c->descriptor.path, pathOf(imageFile), ss(name));
    if (!description.isEmpty()) p.description = ss(description);
    p.image.usedOnly = usedOnly;
    p.image.encrypt = encrypt;
    core::ProfileStore store(ss(directory()));
    auto saved = store.save(p);
    if (!saved) return errorToVariant(saved.error());
    reload();
    return {};
}

QVariantMap ProfilesModel::remove(int row) {
    if (row < 0 || row >= rowCount()) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no such profile"));
    core::ProfileStore store(ss(directory()));
    auto r = store.remove(m_cards[static_cast<std::size_t>(row)].stored.file);
    if (!r) return errorToVariant(r.error());
    reload();
    return {};
}

} // namespace drstein::ui
