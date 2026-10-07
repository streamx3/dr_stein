// SPDX-License-Identifier: MIT
#include "models.hpp"

#include "drstein/core/format.hpp"
#include "util.hpp"

namespace drstein::ui {

// ---- SourceListModel ------------------------------------------------------------

SourceListModel::SourceListModel(QObject* parent) : QAbstractListModel(parent) {}

QVariant SourceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& s = m_items[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Id: return qs(s.id);
    case Group: return qs(core::toString(s.group));
    case Name: return qs(s.name);
    case Subtitle: return qs(s.subtitle);
    case SizeText: return qs(core::sizeText(s.sizeBytes));
    case Icon: return qs(s.icon);
    case Kind: return s.kind == core::SourceKind::Disk ? "disk" : "image";
    case Path: return qs(s.path);
    case Selected: return s.id == m_selected;
    case Locked: return s.image && s.image->locked;
    case Target: return s.id == m_target;
    }
    return {};
}

QHash<int, QByteArray> SourceListModel::roleNames() const {
    return {{Id, "id"}, {Group, "group"}, {Name, "name"}, {Subtitle, "subtitle"}, {SizeText, "sizeText"},
            {Icon, "icon"}, {Kind, "kind"}, {Path, "path"}, {Selected, "selected"}, {Locked, "locked"}, {Target, "target"}};
}

void SourceListModel::setItems(std::vector<core::SourceDescriptor> items) {
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    Q_EMIT countChanged();
}

const core::SourceDescriptor* SourceListModel::find(const std::string& id) const {
    for (const auto& s : m_items)
        if (s.id == id) return &s;
    return nullptr;
}

int SourceListModel::indexOf(const std::string& id) const {
    for (std::size_t i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return static_cast<int>(i);
    return -1;
}

void SourceListModel::setSelected(const std::string& id) {
    const int before = indexOf(m_selected);
    m_selected = id;
    const int after = indexOf(id);
    if (before >= 0) Q_EMIT dataChanged(index(before), index(before), {Selected});
    if (after >= 0) Q_EMIT dataChanged(index(after), index(after), {Selected});
}

void SourceListModel::setTarget(const std::string& id) {
    const int before = indexOf(m_target);
    m_target = id;
    const int after = indexOf(id);
    if (before >= 0) Q_EMIT dataChanged(index(before), index(before), {Target});
    if (after >= 0) Q_EMIT dataChanged(index(after), index(after), {Target});
}

void SourceListModel::replace(const core::SourceDescriptor& item) {
    const int i = indexOf(item.id);
    if (i < 0) return;
    m_items[static_cast<std::size_t>(i)] = item;
    Q_EMIT dataChanged(index(i), index(i));
}

void SourceListModel::remove(const std::string& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    beginRemoveRows({}, i, i);
    m_items.erase(m_items.begin() + i);
    endRemoveRows();
    Q_EMIT countChanged();
}

void SourceListModel::add(const core::SourceDescriptor& item) {
    if (indexOf(item.id) >= 0) {
        replace(item);
        return;
    }
    const int i = rowCount();
    beginInsertRows({}, i, i);
    m_items.push_back(item);
    endInsertRows();
    Q_EMIT countChanged();
}

QVariantMap SourceListModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

// ---- TopologyModel --------------------------------------------------------------

TopologyModel::TopologyModel(QObject* parent) : QAbstractListModel(parent) {}

QVariant TopologyModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Path: return pathToVariant(r.path);
    case Depth: return r.depth;
    case KindLabel: return qs(r.kindLabel);
    case Name: return qs(r.name);
    case Content: return qs(r.content);
    case SizeText: return qs(r.sizeText);
    case Health: return healthName(r.health);
    case HealthText: return qs(r.healthText);
    case Segment: return r.segment;
    case IsMetadata: return r.isMetadata;
    case MetadataIndex: return r.metadataIndex;
    case CanBrowse: return r.canBrowse;
    case CanInspect: return r.canInspect;
    case CanRepair: return r.canRepair;
    case CanMount: return r.canMount;
    case IsLocked: return r.isLockedContainer;
    case UsedFraction: return r.usedFraction;
    case Selected: return index.row() == m_selectedRow;
    case OsDevice: return qs(r.osDevice);
    case Mountpoint: return qs(r.mountpoint);
    }
    return {};
}

QHash<int, QByteArray> TopologyModel::roleNames() const {
    return {{Path, "path"}, {Depth, "depth"}, {KindLabel, "kindLabel"}, {Name, "name"}, {Content, "content"},
            {SizeText, "sizeText"}, {Health, "health"}, {HealthText, "healthText"}, {Segment, "segment"},
            {IsMetadata, "isMetadata"}, {MetadataIndex, "metadataIndex"}, {CanBrowse, "canBrowse"}, {CanInspect, "canInspect"},
            {CanRepair, "canRepair"}, {CanMount, "canMount"}, {IsLocked, "isLocked"}, {UsedFraction, "usedFraction"}, {Selected, "selected"},
            {OsDevice, "osDevice"}, {Mountpoint, "mountpoint"}};
}

void TopologyModel::setRows(std::vector<core::TopologyRow> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    if (m_selectedRow >= rowCount()) m_selectedRow = rowCount() ? 0 : -1;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT selectedRowChanged();
}

void TopologyModel::setSelectedRow(int row) {
    if (row < -1 || row >= rowCount()) row = -1;
    if (row == m_selectedRow) return;
    const int before = m_selectedRow;
    m_selectedRow = row;
    if (before >= 0) Q_EMIT dataChanged(index(before), index(before), {Selected});
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row), {Selected});
    Q_EMIT selectedRowChanged();
}

int TopologyModel::rowForPath(const core::NodePath& path) const {
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].isMetadata && m_rows[i].path == path) return static_cast<int>(i);
    return -1;
}

QVariantMap TopologyModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

// ---- NodeDetailsObject ----------------------------------------------------------

void NodeDetailsObject::set(const core::NodeDetails& d, bool valid) {
    m_valid = valid;
    m_kindLabel = qs(d.kindLabel);
    m_title = qs(d.title);
    m_regionText = qs(d.regionText);
    m_rows.clear();
    for (const auto& r : d.rows) {
        QVariantMap m;
        m["key"] = qs(r.key);
        m["value"] = qs(r.value);
        m["mono"] = r.mono;
        m_rows.push_back(m);
    }
    m_notes.clear();
    for (const auto& n : d.notes) {
        QVariantMap m;
        m["severity"] = healthName(n.severity);
        m["code"] = qs(n.code);
        m["message"] = qs(n.message);
        m_notes.push_back(m);
    }
    m_subvolumes.clear();
    for (const auto& s : d.subvolumes) {
        QVariantMap m;
        m["kind"] = qs(s.kind);
        m["name"] = qs(s.name);
        m["parent"] = qs(s.parent);
        m["note"] = qs(s.note);
        m_subvolumes.push_back(m);
    }
    m_usedFraction = d.usedFraction.value_or(-1);
    m_usedLabel = qs(d.usedLabel);
    m_usedText = qs(d.usedText);
    m_canBrowse = d.canBrowse;
    m_canInspect = d.canInspect;
    m_canRepair = d.canRepair;
    m_canMount = d.canMount;
    m_locked = d.isLockedContainer;
    m_osDevice = qs(d.osDevice);
    m_mountpoint = qs(d.mountpoint);
    Q_EMIT changed();
}

void NodeDetailsObject::setUsage(double fraction, const QString& label, const QString& text) {
    m_usedFraction = fraction;
    m_usedLabel = label;
    m_usedText = text;
    Q_EMIT changed();
}

} // namespace drstein::ui
