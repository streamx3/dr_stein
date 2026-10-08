// SPDX-License-Identifier: MIT
// Conversions at the core ↔ Qt boundary. The only place std::string meets QString.
#pragma once

#include "drstein/core/format.hpp"
#include "drstein/core/topology.hpp"

#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <string>

namespace drstein::ui {

inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline std::string ss(const QString& s) { return s.toStdString(); }
inline std::filesystem::path pathOf(const QUrl& url) { return std::filesystem::path(ss(url.isLocalFile() ? url.toLocalFile() : url.toString())); }
inline QString pathText(const std::filesystem::path& p) { return qs(p.string()); }

inline QVariantList pathToVariant(const core::NodePath& path) {
    QVariantList out;
    for (int i : path) out.push_back(i);
    return out;
}

inline core::NodePath pathFromVariant(const QVariantList& list) {
    core::NodePath out;
    for (const auto& v : list) out.push_back(v.toInt());
    return out;
}

inline QVariantMap errorToVariant(const stein::Error& e) {
    const auto p = core::present(e);
    QVariantMap m;
    m["title"] = qs(p.title);
    m["message"] = qs(p.message);
    m["hint"] = qs(p.hint);
    m["severity"] = p.severity == core::Severity::Error ? "error" : p.severity == core::Severity::Warning ? "warning" : "info";
    m["needsElevation"] = p.needsElevation;
    m["busy"] = p.busy;
    m["integrity"] = p.integrity;
    m["osCode"] = static_cast<qlonglong>(e.osCode());   // int64_t is long on Linux, which QVariant lacks
    m["fullDiskAccess"] = false;
    return m;
}

inline QString healthName(stein::layout::Validity v) {
    switch (v) {
    case stein::layout::Validity::Ok: return "ok";
    case stein::layout::Validity::Info: return "info";
    case stein::layout::Validity::Warning: return "warning";
    case stein::layout::Validity::Error: return "error";
    }
    return "ok";
}

} // namespace drstein::ui
