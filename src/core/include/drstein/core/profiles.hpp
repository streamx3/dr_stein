// SPDX-License-Identifier: MIT
// The Profiles view: a directory of app::Profile JSON files, one card each,
// three buttons each (backup / restore / verify), all dry-runnable.
#pragma once

#include "stein/app/scenario.hpp"
#include "stein/core/progress.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace drstein::core {

struct StoredProfile {
    std::filesystem::path file;
    stein::app::Profile profile;
};

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path dir);
    const std::filesystem::path& dir() const { return m_dir; }
    stein::Expected<std::vector<StoredProfile>> list() const;
    // Saves under `existing` or a new file named after the profile.
    stein::Expected<StoredProfile> save(const stein::app::Profile& profile, std::optional<std::filesystem::path> existing = {}) const;
    stein::Expected<void> remove(const std::filesystem::path& file) const;

private:
    std::filesystem::path m_dir;
};

struct ProfileCard {
    StoredProfile stored;
    stein::app::Status status;
    std::string kicker;           // the description's first words or the file name
    std::string diskText;         // "Samsung SSD 980 · present" / "not present"
    std::string selectorText;     // "serial S64ANX0R123456" / "model "KXG60" · 1 TB ±2%"
    std::string imageText;        // "ws.stein · complete" / "ws.stein · missing"
    std::string madeText;         // "2 days ago · 188 GB" (what the manifest says)
    std::string fitsText;         // "yes · same size" / "— (no disk)"
    std::string policyText;       // "lock target to identity · verify before restore · ..."
    std::string verifyLevelText;  // "level 2"
    bool diskPresent = false, imagePresent = false, imageComplete = false;
    bool canBackup = false, canRestore = false, canVerify = false;
    std::vector<std::string> warnings;
};
ProfileCard profileCard(const StoredProfile& stored, stein::platform::Platform* platform = nullptr);

// A new profile for a disk the OS lists (identity from the DiskInfo) or a regular file (tests).
stein::app::Profile profileFromDisk(const stein::platform::DiskInfo& disk, const std::filesystem::path& image, std::string name);
stein::app::Profile profileFromFile(const std::filesystem::path& file, const std::filesystem::path& image, std::string name);

enum class Scenario { Backup, Restore, Verify };
std::string_view toString(Scenario s);
stein::Expected<stein::app::ScenarioResult> runScenario(Scenario scenario, const stein::app::Profile& profile, const stein::app::RunOptions& options,
                                                        stein::Progress& progress);

} // namespace drstein::core
