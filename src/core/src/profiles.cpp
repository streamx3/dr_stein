// SPDX-License-Identifier: MIT
#include "drstein/core/profiles.hpp"

#include "drstein/core/format.hpp"
#include "stein/core/strings.hpp"

#include <algorithm>

namespace drstein::core {

using namespace stein;

ProfileStore::ProfileStore(std::filesystem::path dir) : m_dir(std::move(dir)) {}

Expected<std::vector<StoredProfile>> ProfileStore::list() const {
    std::vector<StoredProfile> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(m_dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;
        auto p = app::Profile::load(entry.path());
        if (!p) continue;   // a broken file must not hide the others; the UI can list it separately later
        out.push_back({entry.path(), std::move(*p)});
    }
    std::sort(out.begin(), out.end(), [](const StoredProfile& a, const StoredProfile& b) { return toLower(a.profile.name) < toLower(b.profile.name); });
    return out;
}

namespace {

std::string safeFileName(std::string name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out += ok ? c : '_';
    }
    if (out.empty()) out = "profile";
    return out;
}

} // namespace

Expected<StoredProfile> ProfileStore::save(const app::Profile& profile, std::optional<std::filesystem::path> existing) const {
    if (auto v = profile.validate(); !v) return fail(v.error());
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    std::filesystem::path file = existing ? *existing : m_dir / (safeFileName(profile.name) + ".json");
    if (!existing) {
        int n = 2;
        while (std::filesystem::exists(file, ec)) file = m_dir / (safeFileName(profile.name) + "-" + std::to_string(n++) + ".json");
    }
    if (auto w = profile.save(file); !w) return fail(w.error());
    return StoredProfile{file, profile};
}

Expected<void> ProfileStore::remove(const std::filesystem::path& file) const {
    std::error_code ec;
    if (!std::filesystem::remove(file, ec)) return fail(ErrorCategory::Io, "could not delete " + file.string() + ": " + ec.message());
    return {};
}

namespace {

std::string selectorText(const app::TargetSelector& t) {
    std::vector<std::string> parts;
    if (!t.serial.empty()) parts.push_back("serial " + t.serial);
    if (!t.wwn.empty()) parts.push_back("wwn " + t.wwn);
    if (!t.model.empty()) parts.push_back("model \"" + t.model + "\"");
    if (t.sizeBytes) {
        std::string s = sizeText(*t.sizeBytes);
        if (t.sizeTolerance > 0) s += " ±" + std::to_string(static_cast<int>(t.sizeTolerance * 100 + 0.5)) + "%";
        parts.push_back(s);
    }
    if (parts.empty() && !t.osPath.empty()) parts.push_back("path " + t.osPath + " (unlocked)");
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
    return out;
}

std::string policyText(const app::Policy& p) {
    std::vector<std::string> parts;
    if (p.lockTarget) parts.push_back("lock target to identity");
    if (p.requireElevated) parts.push_back("require elevation");
    if (p.allowSmallerTarget) parts.push_back("allow smaller target");
    if (p.verifyBeforeRestore != app::VerifyLevel::None) parts.push_back("verify before restore (" + std::string(app::toString(p.verifyBeforeRestore)) + ")");
    if (p.verifyAfterRestore) parts.push_back("verify after restore");
    if (p.verifyAfterBackup) parts.push_back("verify after backup");
    parts.push_back(p.unmountTarget ? "unmount first" : "refuse while mounted");
    if (p.repairTableAfterRestore) parts.push_back("repair GPT backup on larger disk");
    std::string out;
    for (const auto& x : parts) out += (out.empty() ? "" : " · ") + x;
    return out;
}

} // namespace

ProfileCard profileCard(const StoredProfile& stored, platform::Platform* platform) {
    ProfileCard c;
    c.stored = stored;
    const auto& p = stored.profile;
    app::RunOptions o;
    o.platform = platform;
    c.status = app::status(p, o);
    c.kicker = p.description.empty() ? stored.file.stem().string() : p.description;
    c.diskPresent = c.status.target.has_value();
    if (c.diskPresent) {
        const auto& d = c.status.target->disk;
        const std::string what = d.model.empty() ? d.osPath : d.model;
        c.diskText = what + " · present" + (c.status.target->byPath ? " (by path)" : "") + (c.status.target->isFile ? " (file)" : "");
    } else {
        c.diskText = "not present";
        if (!c.status.target.error().message().empty()) c.warnings.push_back(c.status.target.error().message());
    }
    c.selectorText = selectorText(p.target);
    const std::string imageName = p.image.path.filename().string();
    c.imagePresent = c.status.imageExists;
    c.imageComplete = c.status.imageComplete;
    c.imageText = imageName + " · " + (!c.imagePresent ? "not created yet" : c.imageComplete ? "complete" : "incomplete");
    if (c.imagePresent) {
        c.madeText = c.status.imageCreated.empty() ? "" : c.status.imageCreated;
        if (c.status.imageSourceBytes) c.madeText += (c.madeText.empty() ? "" : " · ") + sizeText(c.status.imageSourceBytes) + " source";
        if (!c.status.imageSourceName.empty()) c.madeText += (c.madeText.empty() ? "" : " · ") + c.status.imageSourceName;
        if (c.madeText.empty()) c.madeText = "image present";
    } else {
        c.madeText = "—";
    }
    if (!c.diskPresent) c.fitsText = "— (no disk)";
    else if (!c.imagePresent) c.fitsText = "— (no image)";
    else if (c.status.imageFitsTarget) {
        const auto target = c.status.target->disk.geometry.sizeBytes;
        c.fitsText = target == c.status.imageSourceBytes ? "yes · same size" : "yes · target is larger";
    } else c.fitsText = p.policy.allowSmallerTarget ? "no · smaller target allowed by policy" : "no · target is smaller";
    c.policyText = policyText(p.policy);
    c.verifyLevelText = "level " + std::to_string(static_cast<int>(p.policy.verifyBeforeRestore));
    c.canBackup = c.diskPresent;
    c.canRestore = c.diskPresent && c.imagePresent && c.imageComplete && (c.status.imageFitsTarget || p.policy.allowSmallerTarget);
    c.canVerify = c.imagePresent;
    for (const auto& w : c.status.warnings) c.warnings.push_back(w);
    return c;
}

app::Profile profileFromDisk(const platform::DiskInfo& d, const std::filesystem::path& image, std::string name) {
    app::Profile p;
    p.name = std::move(name);
    p.image.path = image;
    p.target.serial = d.serial;
    p.target.wwn = d.wwn;
    p.target.model = d.model;
    p.target.sizeBytes = d.geometry.sizeBytes;
    p.target.sizeTolerance = 0.02;
    p.target.osPath = d.osPath;
    p.target.allowRemovable = d.removable;
    p.target.allowVirtual = d.isVirtual;
    p.policy.requireElevated = true;
    p.description = d.model + (d.serial.empty() ? "" : " sn:" + d.serial) + ", " + sizeText(d.geometry.sizeBytes) + " at " + d.osPath;
    return p;
}

app::Profile profileFromFile(const std::filesystem::path& file, const std::filesystem::path& image, std::string name) {
    app::Profile p;
    p.name = std::move(name);
    p.image.path = image;
    p.target.osPath = file.string();
    p.target.allowVirtual = true;
    p.policy.requireElevated = false;
    p.description = "regular file target (testing)";
    return p;
}

std::string_view toString(Scenario s) {
    switch (s) {
    case Scenario::Backup: return "backup";
    case Scenario::Restore: return "restore";
    case Scenario::Verify: return "verify";
    }
    return "?";
}

Expected<app::ScenarioResult> runScenario(Scenario scenario, const app::Profile& profile, const app::RunOptions& options, Progress& progress) {
    switch (scenario) {
    case Scenario::Backup: return app::backup(profile, options, progress);
    case Scenario::Restore: return app::restore(profile, options, progress);
    case Scenario::Verify: return app::verify(profile, options, progress);
    }
    return fail(ErrorCategory::InvalidArgument, "unknown scenario");
}

} // namespace drstein::core
