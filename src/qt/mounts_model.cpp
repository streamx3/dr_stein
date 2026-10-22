// SPDX-License-Identifier: MIT
#include "mounts_model.hpp"

#include "drstein/core/mounts.hpp"
#include "util.hpp"
#include "workspace.hpp"

namespace drstein::ui {

namespace {
MountsModel* g_instance = nullptr;
}

MountsModel::MountsModel(QObject* parent) : QAbstractListModel(parent) {
    // The workspace owns the manager and announces every change (its own mounts, unmounts, the
    // watchdog's drops); the model only mirrors the records.
    connect(Workspace::instance(), &Workspace::mountsChanged, this, &MountsModel::reloadRows);
    m_watchdog.setInterval(2000);
    connect(&m_watchdog, &QTimer::timeout, this, &MountsModel::poll);
    reloadRows();
}

MountsModel* MountsModel::instance() {
    if (!g_instance) g_instance = new MountsModel();
    return g_instance;
}

MountsModel* MountsModel::create(QQmlEngine*, QJSEngine*) {
    MountsModel* m = instance();
    QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);
    return m;
}

QVariant MountsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Id: return r.id;
    case Mountpoint: return r.mountpoint;
    case What: return r.what;
    case Running: return r.running;
    case Error: return r.error;
    case SourceId: return r.sourceId;
    }
    return {};
}

QHash<int, QByteArray> MountsModel::roleNames() const {
    return {{Id, "id"}, {Mountpoint, "mountpoint"}, {What, "what"}, {Running, "running"}, {Error, "error"}, {SourceId, "sourceId"}};
}

bool MountsModel::available() const { return core::MountManager::available(); }

void MountsModel::reloadRows() {
    beginResetModel();
    m_rows.clear();
    for (const auto& m : Workspace::instance()->mounts().list()) m_rows.push_back({m.id, pathText(m.mountpoint), qs(m.what), qs(m.error), qs(m.sourceId), m.running});
    endResetModel();
    Q_EMIT countChanged();
    // Nothing to watch while nothing is mounted.
    if (m_rows.empty()) m_watchdog.stop();
    else if (!m_watchdog.isActive()) m_watchdog.start();
}

void MountsModel::reload() { Workspace::instance()->refreshMounts(); }

void MountsModel::unmount(int row) {
    if (row < 0 || row >= rowCount()) return;
    if (auto r = Workspace::instance()->mounts().unmount(m_rows[static_cast<std::size_t>(row)].id); !r) Workspace::instance()->reportError(r.error());
    Workspace::instance()->refreshMounts();
}

void MountsModel::unmountAll() {
    Workspace::instance()->mounts().unmountAll();
    Workspace::instance()->refreshMounts();
}

void MountsModel::reveal(int row) {
    if (row < 0 || row >= rowCount()) return;
    Workspace::instance()->revealPath(m_rows[static_cast<std::size_t>(row)].mountpoint);
}

QVariantMap MountsModel::get(int row) const {
    QVariantMap m;
    const auto idx = index(row);
    if (!idx.isValid()) return m;
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) m[QString::fromUtf8(it.value())] = data(idx, it.key());
    return m;
}

void MountsModel::poll() {
    const auto gone = Workspace::instance()->mounts().reapEnded();
    if (gone.empty()) return;
    for (const auto& m : gone) {
        QVariantMap e;
        e["title"] = "Unmounted outside Dr Stein";
        e["message"] = pathText(m.mountpoint) + (m.error.empty() ? " was unmounted; the export ended." : " ended: " + qs(m.error));
        e["hint"] = "";
        e["severity"] = m.error.empty() ? "info" : "warning";
        Q_EMIT Workspace::instance()->error(e);
    }
    Workspace::instance()->refreshMounts();
}

} // namespace drstein::ui
