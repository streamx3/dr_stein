// SPDX-License-Identifier: MIT
// The Partitions view's facade over core::EditSession: pending operations,
// the preview diff, both bars, and apply as a job.
#pragma once

#include "drstein/core/partition_edit.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace drstein::ui {

class PartitionEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(bool hasTable READ hasTable NOTIFY changed)
    Q_PROPERTY(QString scheme READ scheme NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString changedRanges READ changedRanges NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(bool destructive READ destructive NOTIFY changed)
    Q_PROPERTY(QVariantList pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantList previewRows READ previewRows NOTIFY changed)
    Q_PROPERTY(QVariantList baseSegments READ baseSegments NOTIFY changed)
    Q_PROPERTY(QVariantList previewSegments READ previewSegments NOTIFY changed)
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY changed)
    Q_PROPERTY(QVariantList typeChoices READ typeChoices NOTIFY changed)
    Q_PROPERTY(bool isRealDevice READ isRealDevice NOTIFY changed)
    Q_PROPERTY(QString deviceIdentity READ deviceIdentity NOTIFY changed)
    Q_PROPERTY(QString freeSpaceHint READ freeSpaceHint NOTIFY changed)
    Q_PROPERTY(bool canRepair READ canRepair NOTIFY changed)

public:
    explicit PartitionEditor(QObject* parent = nullptr);

    bool available() const { return m_session.has_value(); }
    QString unavailableReason() const { return m_reason; }
    bool hasTable() const;
    QString scheme() const;
    QString summary() const;
    QString changedRanges() const;
    int pendingCount() const;
    bool destructive() const;
    QVariantList pending() const;
    QVariantList previewRows() const;
    QVariantList baseSegments() const;
    QVariantList previewSegments() const;
    QVariantList partitions() const;
    QVariantList typeChoices() const;
    bool isRealDevice() const;
    QString deviceIdentity() const;
    QString freeSpaceHint() const;
    bool canRepair() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantMap addPartition(const QString& start, const QString& size, const QString& end, const QString& type, const QString& name, bool wipe);
    Q_INVOKABLE QVariantMap editPartition(int index, const QString& start, const QString& size, const QString& end, const QString& type, const QString& name, bool nameChanged);
    Q_INVOKABLE QVariantMap deletePartition(int index);
    Q_INVOKABLE QVariantMap repairTable();
    Q_INVOKABLE QVariantMap newTable(const QString& scheme);
    Q_INVOKABLE QVariantMap wipe(int index);
    Q_INVOKABLE void undoLast();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void apply();

Q_SIGNALS:
    void changed();
    void applied(bool ok);

private:
    QVariantMap report(const stein::Expected<void>& r);
    QVariantList segmentsToVariant(const std::vector<core::Segment>& segs) const;

    std::optional<core::EditSession> m_session;
    QString m_reason;
    std::string m_sourceId;
};

} // namespace drstein::ui
