// SPDX-License-Identifier: MIT
// The column view of Browse (as macOS Finder shows a folder): one column per breadcrumb, the
// last one current, and a peek into the folder selected there. What the columns are, what a
// click in one of them means and the listings of the columns that are not current; the
// facade only turns these into models.
#pragma once

#include "drstein/core/browse.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace drstein::core {

struct ColumnPlan {
    stein::fs::Inode dir;        // the directory the column lists
    std::string title;           // the crumb's name ("/" for the root); the folder's name for the peek
    std::size_t crumb = 0;       // the breadcrumb the column is: its position, and crumbs.size() for the peek
    bool current = false;        // the last breadcrumb: the list with the selection
    bool peek = false;           // the folder selected in the current column, nothing selected in it
    std::string selectName;      // an ancestor's row shown selected (the next crumb's name); empty otherwise
    bool operator==(const ColumnPlan&) const = default;
};

// Column k lists breadcrumb k; a selected directory in the current column adds a peek column.
// No crumbs, no columns.
std::vector<ColumnPlan> planColumns(const std::vector<Crumb>& crumbs, const Entry* selectedInCurrent);

// How many leading columns list the same directories before and after: those keep their views
// (and scroll positions) and only update their flags, the rest are replaced.
std::size_t keptColumns(const std::vector<ColumnPlan>& before, const std::vector<ColumnPlan>& after);

// The row named `name`, -1 when there is none. An exact match wins; when the filesystem ignores
// case, so does the comparison, for ASCII letters only (a path typed in another case).
int rowOfName(const Listing& listing, std::string_view name, bool caseSensitive);

enum class ColumnClick {
    SelectInCurrent,   // select the row
    JumpThenSelect,    // an ancestor: jump to its crumb (which re-selects the folder left), then select the row
    EnterThenSelect,   // the peek: enter the selected folder, then select the row
    Ignore,            // no such column
};
ColumnClick classifyClick(std::size_t column, const std::vector<ColumnPlan>& plans);

// Listings of directories that are not current, by directory inode. The filesystem is read-only,
// so a listing never goes stale while the same Browser reads it; whoever opens another one
// clears the cache. Bounded: past `capacity` (at least 1) the least recently used listing goes.
class ListingCache {
public:
    explicit ListingCache(std::size_t capacity = 64);

    // A cached listing, which now counts as the most recently used; null when there is none.
    std::shared_ptr<const Listing> find(const stein::fs::Inode& dir);
    // Caches a listing as the most recently used, replacing one of the same directory.
    void put(std::shared_ptr<const Listing> listing);
    // The cached listing, or the one `browser` reads now and is cached; a read error is
    // returned and not cached, so the next call reads again.
    stein::Expected<std::shared_ptr<const Listing>> get(Browser& browser, const stein::fs::Inode& dir);
    void clear();

    std::size_t size() const { return m_listings.size(); }
    std::size_t capacity() const { return m_capacity; }

private:
    std::size_t m_capacity;
    std::vector<std::shared_ptr<const Listing>> m_listings;   // the most recently used first
};

} // namespace drstein::core
