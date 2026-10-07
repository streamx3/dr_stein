// SPDX-License-Identifier: MIT
// A stein::Report tree flattened into rows for the job card.
#pragma once

#include "stein/core/report.hpp"

#include <QAbstractListModel>
#include <QQmlEngine>

#include <vector>

namespace drstein::ui {

class ReportModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by JobRunner")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { Title = Qt::UserRole + 1, Status, Detail, Lines, Duration, Depth };
    explicit ReportModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setReport(const stein::Report* report);   // snapshot; nullptr clears
    void clear() { setReport(nullptr); }
    Q_INVOKABLE QString text() const { return m_text; }

Q_SIGNALS:
    void countChanged();

private:
    struct Row {
        QString title, status, detail, lines, duration;
        int depth = 0;
    };
    void append(const stein::Report& r, int depth);
    std::vector<Row> m_rows;
    QString m_text;
};

} // namespace drstein::ui
