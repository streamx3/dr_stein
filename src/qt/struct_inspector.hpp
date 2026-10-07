// SPDX-License-Identifier: MIT
// The Hex view's facade: the structures of the selected node, the bytes of
// the chosen one as 16-byte rows, its parsed fields, and the overlay editor.
#pragma once

#include "drstein/core/structs.hpp"

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

#include <memory>
#include <optional>
#include <vector>

namespace drstein::ui {

class HexRowsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by StructInspector")
public:
    enum Roles { OffsetText = Qt::UserRole + 1, Offset, Hex, Ascii, Dirty };
    explicit HexRowsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void set(const core::StructEditor* editor, stein::ByteCount displayBase);
    void refreshDirty(const core::StructEditor* editor);

private:
    struct Row {
        QString offsetText;
        qulonglong offset = 0;
        QStringList hex;
        QString ascii;
        QVariantList dirty;   // 16 bools
    };
    std::vector<Row> m_rows;
};

class FieldsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by StructInspector")
public:
    enum Roles { Name = Qt::UserRole + 1, TypeName, Value, Display, Pretty, Doc, Message, OffsetText, Offset, Size, Validity, Depth, IsStruct, Editable, Dirty, Selected };
    explicit FieldsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void set(std::vector<core::FieldRow> rows, const core::StructEditor* editor, stein::ByteCount displayBase);
    const std::vector<core::FieldRow>& rows() const { return m_rows; }
    void setSelected(int row);
    int selected() const { return m_selected; }

private:
    std::vector<core::FieldRow> m_rows;
    std::vector<bool> m_dirty;
    stein::ByteCount m_displayBase = 0;
    int m_selected = -1;
};

class StructInspector : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QVariantList structs READ structs NOTIFY changed)
    Q_PROPERTY(int currentStruct READ currentStruct WRITE setCurrentStruct NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString where READ where NOTIFY changed)
    Q_PROPERTY(QString validity READ validity NOTIFY changed)
    Q_PROPERTY(HexRowsModel* rows READ rows CONSTANT)
    Q_PROPERTY(FieldsModel* fields READ fields CONSTANT)
    Q_PROPERTY(int selectedField READ selectedField NOTIFY selectionChanged)
    Q_PROPERTY(qulonglong selectionStart READ selectionStart NOTIFY selectionChanged)
    Q_PROPERTY(qulonglong selectionEnd READ selectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedFieldLine READ selectedFieldLine NOTIFY selectionChanged)
    Q_PROPERTY(int dirtyCount READ dirtyCount NOTIFY changed)
    Q_PROPERTY(QString dirtyRanges READ dirtyRanges NOTIFY changed)
    Q_PROPERTY(bool canWrite READ canWrite NOTIFY changed)
    Q_PROPERTY(int fixableChecksums READ fixableChecksums NOTIFY changed)

public:
    explicit StructInspector(QObject* parent = nullptr);

    bool available() const { return !m_structs.empty(); }
    QString unavailableReason() const { return m_reason; }
    QVariantList structs() const;
    int currentStruct() const { return m_current; }
    void setCurrentStruct(int i);
    QString label() const;
    QString where() const;
    QString validity() const;
    HexRowsModel* rows() { return &m_rows; }
    FieldsModel* fields() { return &m_fields; }
    int selectedField() const { return m_fields.selected(); }
    qulonglong selectionStart() const;
    qulonglong selectionEnd() const;
    QString selectedFieldLine() const;
    int dirtyCount() const;
    QString dirtyRanges() const;
    bool canWrite() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void selectField(int row);
    Q_INVOKABLE void selectByte(qulonglong displayOffset);
    Q_INVOKABLE QVariantMap editField(int row, const QString& text);   // empty map on success
    Q_INVOKABLE void revert();
    // Sets every CRC field the library flags as "mismatch; computed 0x..." to that value,
    // innermost first, until nothing is left to fix. Returns how many fields changed.
    Q_INVOKABLE int fixChecksums();
    int fixableChecksums() const;
    Q_INVOKABLE QVariantMap write();
    Q_INVOKABLE QVariantMap saveHeader(const QUrl& file);
    Q_INVOKABLE QVariantMap restoreFromFile(const QUrl& file);

Q_SIGNALS:
    void changed();
    void selectionChanged();
    void written();

private:
    void loadCurrent();
    void refreshViews();

    std::vector<core::StructRef> m_structs;
    int m_current = -1;
    std::unique_ptr<core::StructEditor> m_editor;
    HexRowsModel m_rows;
    FieldsModel m_fields;
    QString m_reason;
};

} // namespace drstein::ui
