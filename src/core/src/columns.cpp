// SPDX-License-Identifier: MIT
#include "drstein/core/columns.hpp"

#include <algorithm>
#include <utility>

namespace drstein::core {

using namespace stein;

namespace {

// Byte-wise and locale-free: a library's tolower may fold the bytes of UTF-8 sequences as if
// they were Latin-1 letters.
char asciiLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool asciiEqualIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

} // namespace

std::vector<ColumnPlan> planColumns(const std::vector<Crumb>& crumbs, const Entry* selectedInCurrent) {
    std::vector<ColumnPlan> plans;
    if (crumbs.empty()) return plans;
    plans.reserve(crumbs.size() + 1);
    for (std::size_t k = 0; k < crumbs.size(); ++k) {
        ColumnPlan c;
        c.dir = crumbs[k].inode;
        c.title = crumbs[k].name;
        c.crumb = k;
        c.current = k + 1 == crumbs.size();
        if (!c.current) c.selectName = crumbs[k + 1].name;
        plans.push_back(std::move(c));
    }
    if (selectedInCurrent && selectedInCurrent->isDir()) {
        ColumnPlan c;
        c.dir = selectedInCurrent->entry.inode;
        c.title = selectedInCurrent->entry.name;
        c.crumb = crumbs.size();
        c.peek = true;
        plans.push_back(std::move(c));
    }
    return plans;
}

std::size_t keptColumns(const std::vector<ColumnPlan>& before, const std::vector<ColumnPlan>& after) {
    const std::size_t n = std::min(before.size(), after.size());
    std::size_t k = 0;
    while (k < n && before[k].dir == after[k].dir) ++k;
    return k;
}

int rowOfName(const Listing& listing, std::string_view name, bool caseSensitive) {
    const auto& rows = listing.entries;
    auto exact = std::find_if(rows.begin(), rows.end(), [&](const Entry& e) { return e.entry.name == name; });
    if (exact == rows.end() && !caseSensitive)
        exact = std::find_if(rows.begin(), rows.end(), [&](const Entry& e) { return asciiEqualIgnoringCase(e.entry.name, name); });
    return exact == rows.end() ? -1 : static_cast<int>(exact - rows.begin());
}

ColumnClick classifyClick(std::size_t column, const std::vector<ColumnPlan>& plans) {
    if (column >= plans.size()) return ColumnClick::Ignore;
    const ColumnPlan& c = plans[column];
    if (c.current) return ColumnClick::SelectInCurrent;
    if (c.peek) return ColumnClick::EnterThenSelect;
    return ColumnClick::JumpThenSelect;
}

ListingCache::ListingCache(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1)) {}

std::shared_ptr<const Listing> ListingCache::find(const fs::Inode& dir) {
    const auto it = std::find_if(m_listings.begin(), m_listings.end(), [&](const auto& l) { return l->dir == dir; });
    if (it == m_listings.end()) return nullptr;
    std::rotate(m_listings.begin(), it, it + 1);
    return m_listings.front();
}

void ListingCache::put(std::shared_ptr<const Listing> listing) {
    if (!listing) return;
    std::erase_if(m_listings, [&](const auto& l) { return l->dir == listing->dir; });
    m_listings.insert(m_listings.begin(), std::move(listing));
    if (m_listings.size() > m_capacity) m_listings.resize(m_capacity);
}

Expected<std::shared_ptr<const Listing>> ListingCache::get(Browser& browser, const fs::Inode& dir) {
    if (auto cached = find(dir)) return cached;
    auto listing = browser.list(dir);
    if (!listing) return fail(listing.error());
    auto shared = std::make_shared<const Listing>(std::move(*listing));
    put(shared);
    return shared;
}

void ListingCache::clear() { m_listings.clear(); }

} // namespace drstein::core
