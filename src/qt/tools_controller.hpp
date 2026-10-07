// SPDX-License-Identifier: MIT
// The Tools view's facade: surface scan and the capacity (fake-flash) test.
#pragma once

#include "drstein/core/media.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

#include <optional>

namespace drstein::ui {

class ToolsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY changed)
    Q_PROPERTY(QString targetTitle READ targetTitle NOTIFY changed)
    Q_PROPERTY(QString targetPath READ targetPath NOTIFY changed)
    Q_PROPERTY(QString targetIdentity READ targetIdentity NOTIFY changed)
    Q_PROPERTY(QString targetContents READ targetContents NOTIFY changed)
    Q_PROPERTY(QString expectedConfirm READ expectedConfirm NOTIFY changed)
    Q_PROPERTY(bool isRealDevice READ isRealDevice NOTIFY changed)
    Q_PROPERTY(bool canScan READ canScan NOTIFY changed)
    Q_PROPERTY(bool canTest READ canTest NOTIFY changed)
    Q_PROPERTY(QString testBlockedReason READ testBlockedReason NOTIFY changed)
    Q_PROPERTY(int cellCount READ cellCount CONSTANT)
    Q_PROPERTY(QVariantList scanCells READ scanCells NOTIFY scanChanged)
    Q_PROPERTY(bool scanDone READ scanDone NOTIFY scanChanged)
    Q_PROPERTY(bool scanHealthy READ scanHealthy NOTIFY scanChanged)
    Q_PROPERTY(QString scannedText READ scannedText NOTIFY scanChanged)
    Q_PROPERTY(QString unreadableText READ unreadableText NOTIFY scanChanged)
    Q_PROPERTY(QString scanRateText READ scanRateText NOTIFY scanChanged)
    Q_PROPERTY(QStringList badLines READ badLines NOTIFY scanChanged)
    Q_PROPERTY(bool testDone READ testDone NOTIFY testChanged)
    Q_PROPERTY(bool testHealthy READ testHealthy NOTIFY testChanged)
    Q_PROPERTY(QString testVerdict READ testVerdict NOTIFY testChanged)
    Q_PROPERTY(QString testDetail READ testDetail NOTIFY testChanged)
    Q_PROPERTY(QString testSpeedText READ testSpeedText NOTIFY testChanged)

public:
    explicit ToolsController(QObject* parent = nullptr);

    bool hasSource() const;
    QString targetTitle() const;
    QString targetPath() const;
    QString targetIdentity() const;
    QString targetContents() const;
    QString expectedConfirm() const;
    bool isRealDevice() const;
    bool canScan() const;
    bool canTest() const;
    QString testBlockedReason() const;
    int cellCount() const { return 192; }
    QVariantList scanCells() const { return m_cells; }
    bool scanDone() const { return m_scan.has_value(); }
    bool scanHealthy() const { return m_scan && m_scan->healthy; }
    QString scannedText() const { return m_scan ? QString::fromStdString(m_scan->scannedText) : QString(); }
    QString unreadableText() const { return m_scan ? QString::fromStdString(m_scan->unreadableText) : QString(); }
    QString scanRateText() const { return m_scan ? QString::fromStdString(m_scan->rateText) : QString(); }
    QStringList badLines() const;
    bool testDone() const { return m_test.has_value(); }
    bool testHealthy() const { return m_test && m_test->healthy; }
    QString testVerdict() const { return m_test ? QString::fromStdString(m_test->verdict) : QString(); }
    QString testDetail() const { return m_test ? QString::fromStdString(m_test->detail) : QString(); }
    QString testSpeedText() const { return m_test ? QString::fromStdString(m_test->speedText) : QString(); }

    Q_INVOKABLE void surfaceScan();
    Q_INVOKABLE QVariantMap capacityTest(const QString& confirmText, bool quick, bool keepPattern);
    Q_INVOKABLE QString saveScanReport(const QUrl& file);

Q_SIGNALS:
    void changed();
    void scanChanged();
    void testChanged();

private:
    void resetResults();
    void updateCells(stein::ByteCount tested, const std::vector<stein::Region>& bad);

    std::string m_sourceId;
    QVariantList m_cells;
    std::optional<core::ScanSummary> m_scan;
    std::optional<core::CapacitySummary> m_test;
    QString m_scanReportText;
};

} // namespace drstein::ui
