// SPDX-License-Identifier: MIT
// List models over core value types: the sidebar's sources, the topology rows,
// and a details object for the selected node.
#pragma once

#include "drstein/core/source.hpp"
#include "drstein/core/topology.hpp"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVariantList>

#include <vector>

namespace drstein::ui {

class SourceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by Workspace")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { Id = Qt::UserRole + 1, Group, Name, Subtitle, SizeText, Icon, Kind, Path, Selected, Locked };
    explicit SourceListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_items.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(std::vector<core::SourceDescriptor> items);
    const std::vector<core::SourceDescriptor>& items() const { return m_items; }
    const core::SourceDescriptor* find(const std::string& id) const;
    int indexOf(const std::string& id) const;
    void setSelected(const std::string& id);
    void replace(const core::SourceDescriptor& item);   // same id
    void remove(const std::string& id);
    void add(const core::SourceDescriptor& item);

    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    std::vector<core::SourceDescriptor> m_items;
    std::string m_selected;
};

class TopologyModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by Workspace")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedRowChanged)

public:
    enum Roles {
        Path = Qt::UserRole + 1, Depth, KindLabel, Name, Content, SizeText, Health, HealthText, Segment, IsMetadata, MetadataIndex,
        CanBrowse, CanInspect, CanRepair, CanMount, IsLocked, UsedFraction, Selected
    };
    explicit TopologyModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(std::vector<core::TopologyRow> rows);
    const std::vector<core::TopologyRow>& rows() const { return m_rows; }
    int selectedRow() const { return m_selectedRow; }
    void setSelectedRow(int row);
    int rowForPath(const core::NodePath& path) const;   // first non-metadata row with this path
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();
    void selectedRowChanged();

private:
    std::vector<core::TopologyRow> m_rows;
    int m_selectedRow = -1;
};

class NodeDetailsObject : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by Workspace")
    Q_PROPERTY(QString kindLabel READ kindLabel NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString regionText READ regionText NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QVariantList notes READ notes NOTIFY changed)
    Q_PROPERTY(QVariantList subvolumes READ subvolumes NOTIFY changed)
    Q_PROPERTY(bool hasUsed READ hasUsed NOTIFY changed)
    Q_PROPERTY(double usedFraction READ usedFraction NOTIFY changed)
    Q_PROPERTY(QString usedLabel READ usedLabel NOTIFY changed)
    Q_PROPERTY(QString usedText READ usedText NOTIFY changed)
    Q_PROPERTY(bool canBrowse READ canBrowse NOTIFY changed)
    Q_PROPERTY(bool canInspect READ canInspect NOTIFY changed)
    Q_PROPERTY(bool canRepair READ canRepair NOTIFY changed)
    Q_PROPERTY(bool canMount READ canMount NOTIFY changed)
    Q_PROPERTY(bool isLockedContainer READ isLockedContainer NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)

public:
    explicit NodeDetailsObject(QObject* parent = nullptr) : QObject(parent) {}
    void set(const core::NodeDetails& d, bool valid);
    void setUsage(double fraction, const QString& label, const QString& text);

    QString kindLabel() const { return m_kindLabel; }
    QString title() const { return m_title; }
    QString regionText() const { return m_regionText; }
    QVariantList rows() const { return m_rows; }
    QVariantList notes() const { return m_notes; }
    QVariantList subvolumes() const { return m_subvolumes; }
    bool hasUsed() const { return m_usedFraction >= 0; }
    double usedFraction() const { return m_usedFraction; }
    QString usedLabel() const { return m_usedLabel; }
    QString usedText() const { return m_usedText; }
    bool canBrowse() const { return m_canBrowse; }
    bool canInspect() const { return m_canInspect; }
    bool canRepair() const { return m_canRepair; }
    bool canMount() const { return m_canMount; }
    bool isLockedContainer() const { return m_locked; }
    bool valid() const { return m_valid; }

Q_SIGNALS:
    void changed();

private:
    QString m_kindLabel, m_title, m_regionText, m_usedLabel, m_usedText;
    QVariantList m_rows, m_notes, m_subvolumes;
    double m_usedFraction = -1;
    bool m_canBrowse = false, m_canInspect = false, m_canRepair = false, m_canMount = false, m_locked = false, m_valid = false;
};

} // namespace drstein::ui
