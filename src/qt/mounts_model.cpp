// SPDX-License-Identifier: MIT
#include "mounts_model.hpp"

#include "drstein/core/mounts.hpp"
#include "util.hpp"
#include "workspace.hpp"

namespace drstein::ui {

namespace {
MountsModel* g_instance = nullptr;
}

MountsModel::MountsModel(QObject* parent) : QAbstractListModel(parent) {}

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
    }
    return {};
}

QHash<int, QByteArray> MountsModel::roleNames() const {
    return {{Id, "id"}, {Mountpoint, "mountpoint"}, {What, "what"}, {Running, "running"}, {Error, "error"}};
}

bool MountsModel::available() const { return core::MountManager::available(); }

void MountsModel::reload() {
    beginResetModel();
    m_rows.clear();
    for (const auto& m : Workspace::instance()->mounts().list()) m_rows.push_back({m.id, pathText(m.mountpoint), qs(m.what), qs(m.error), m.running});
    endResetModel();
    Q_EMIT countChanged();
}

void MountsModel::unmount(int row) {
    if (row < 0 || row >= rowCount()) return;
    if (auto r = Workspace::instance()->mounts().unmount(m_rows[static_cast<std::size_t>(row)].id); !r) Workspace::instance()->reportError(r.error());
    reload();
}

void MountsModel::unmountAll() {
    Workspace::instance()->mounts().unmountAll();
    reload();
}

} // namespace drstein::ui
