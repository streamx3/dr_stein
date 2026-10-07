// SPDX-License-Identifier: MIT
#include "report_model.hpp"

#include "drstein/core/format.hpp"
#include "util.hpp"

namespace drstein::ui {

ReportModel::ReportModel(QObject* parent) : QAbstractListModel(parent) {}

QVariant ReportModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Title: return r.title;
    case Status: return r.status;
    case Detail: return r.detail;
    case Lines: return r.lines;
    case Duration: return r.duration;
    case Depth: return r.depth;
    }
    return {};
}

QHash<int, QByteArray> ReportModel::roleNames() const {
    return {{Title, "title"}, {Status, "status"}, {Detail, "detail"}, {Lines, "lines"}, {Duration, "duration"}, {Depth, "depth"}};
}

void ReportModel::append(const stein::Report& r, int depth) {
    Row row;
    row.title = qs(r.title());
    row.status = qs(stein::toString(r.status())).toLower();
    QStringList details;
    for (const auto& [k, v] : r.details()) details << qs(k) + " " + qs(v);
    row.detail = details.join(" · ");
    QStringList lines;
    for (const auto& l : r.lines()) lines << qs(l);
    row.lines = lines.join("\n");
    const auto ms = r.duration().count();
    if (ms > 0) row.duration = qs(core::durationText(static_cast<double>(ms) / 1000.0));
    row.depth = depth;
    m_rows.push_back(std::move(row));
    for (const auto& c : r.children()) append(*c, depth + 1);
}

void ReportModel::setReport(const stein::Report* report) {
    beginResetModel();
    m_rows.clear();
    m_text.clear();
    if (report) {
        append(*report, 0);
        m_text = qs(report->toText());
    }
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace drstein::ui
