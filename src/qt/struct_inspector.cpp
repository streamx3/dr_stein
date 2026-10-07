// SPDX-License-Identifier: MIT
#include "struct_inspector.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/opened.hpp"
#include "job_runner.hpp"
#include "stein/block/slice_device.hpp"
#include "util.hpp"
#include "workspace.hpp"

namespace drstein::ui {

// ---- HexRowsModel -------------------------------------------------------------------

QVariant HexRowsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case OffsetText: return r.offsetText;
    case Offset: return r.offset;
    case Hex: return r.hex;
    case Ascii: return r.ascii;
    case Dirty: return r.dirty;
    }
    return {};
}

QHash<int, QByteArray> HexRowsModel::roleNames() const {
    return {{OffsetText, "offsetText"}, {Offset, "offset"}, {Hex, "hex"}, {Ascii, "ascii"}, {Dirty, "dirty"}};
}

void HexRowsModel::set(const core::StructEditor* editor, stein::ByteCount displayBase) {
    beginResetModel();
    m_rows.clear();
    if (editor) {
        const auto bytes = editor->bytes();
        const stein::ByteCount start = editor->current().shownOffset;
        for (std::size_t off = 0; off < bytes.size(); off += 16) {
            Row r;
            r.offset = static_cast<qulonglong>(displayBase + start + off);
            r.offsetText = QString("%1").arg(static_cast<qulonglong>(displayBase + start + off), 8, 16, QChar('0')).toUpper();
            for (std::size_t i = 0; i < 16; ++i) {
                if (off + i < bytes.size()) {
                    const auto v = std::to_integer<unsigned>(bytes[off + i]);
                    r.hex << QString("%1").arg(v, 2, 16, QChar('0')).toUpper();
                    r.ascii += (v >= 32 && v < 127) ? QChar(static_cast<char16_t>(v)) : QChar(0x00B7);
                    r.dirty << editor->isDirty(start + off + i);
                } else {
                    r.hex << "";
                    r.ascii += ' ';
                    r.dirty << false;
                }
            }
            m_rows.push_back(std::move(r));
        }
    }
    endResetModel();
}

void HexRowsModel::refreshDirty(const core::StructEditor* editor) {
    if (!editor) return;
    const auto bytes = editor->bytes();
    const stein::ByteCount start = editor->current().shownOffset;
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        Row& r = m_rows[row];
        bool changed = false;
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t off = row * 16 + i;
            if (off >= bytes.size()) break;
            const auto v = std::to_integer<unsigned>(bytes[off]);
            const QString hex = QString("%1").arg(v, 2, 16, QChar('0')).toUpper();
            const bool dirty = editor->isDirty(start + off);
            if (r.hex[static_cast<qsizetype>(i)] != hex || r.dirty[static_cast<qsizetype>(i)].toBool() != dirty) {
                r.hex[static_cast<qsizetype>(i)] = hex;
                r.dirty[static_cast<qsizetype>(i)] = dirty;
                r.ascii[static_cast<qsizetype>(i)] = (v >= 32 && v < 127) ? QChar(static_cast<char16_t>(v)) : QChar(0x00B7);
                changed = true;
            }
        }
        if (changed) Q_EMIT dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
    }
}

// ---- FieldsModel --------------------------------------------------------------------

QVariant FieldsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& f = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Name: return qs(f.name);
    case TypeName: return qs(f.typeName);
    case Value: return qs(f.value);
    case Display: {
        if (f.isStruct) return "{ … }";
        if (f.type == stein::layout::FieldType::Ascii || f.type == stein::layout::FieldType::Utf16le) return "\"" + qs(f.value) + "\"";
        return qs(f.value);
    }
    case Pretty: return qs(f.pretty);
    case Doc: return qs(f.doc);
    case Message: return qs(f.message);
    case OffsetText: return "0x" + QString::number(static_cast<qulonglong>(m_displayBase + f.offset), 16).toUpper();
    case Offset: return static_cast<qulonglong>(f.offset);
    case Size: return f.size;
    case Validity: return healthName(f.validity);
    case Depth: return f.depth;
    case IsStruct: return f.isStruct;
    case Editable: return f.editable;
    case Dirty: return static_cast<bool>(m_dirty[static_cast<std::size_t>(index.row())]);
    case Selected: return index.row() == m_selected;
    }
    return {};
}

QHash<int, QByteArray> FieldsModel::roleNames() const {
    return {{Name, "name"}, {TypeName, "typeName"}, {Value, "value"}, {Display, "display"}, {Pretty, "pretty"}, {Doc, "doc"},
            {Message, "message"}, {OffsetText, "offsetText"}, {Offset, "offset"}, {Size, "size"}, {Validity, "validity"},
            {Depth, "depth"}, {IsStruct, "isStruct"}, {Editable, "editable"}, {Dirty, "dirty"}, {Selected, "selected"}};
}

void FieldsModel::set(std::vector<core::FieldRow> rows, const core::StructEditor* editor, stein::ByteCount displayBase) {
    beginResetModel();
    m_rows = std::move(rows);
    m_displayBase = displayBase;
    m_dirty.assign(m_rows.size(), false);
    if (editor)
        for (std::size_t i = 0; i < m_rows.size(); ++i)
            for (stein::ByteCount b = m_rows[i].offset; b < m_rows[i].offset + m_rows[i].size; ++b)
                if (editor->isDirty(b)) {
                    m_dirty[i] = true;
                    break;
                }
    if (m_selected >= rowCount()) m_selected = -1;
    endResetModel();
}

void FieldsModel::setSelected(int row) {
    if (row < -1 || row >= rowCount()) row = -1;
    if (row == m_selected) return;
    const int before = m_selected;
    m_selected = row;
    if (before >= 0) Q_EMIT dataChanged(index(before), index(before), {Selected});
    if (row >= 0) Q_EMIT dataChanged(index(row), index(row), {Selected});
}

// ---- StructInspector ----------------------------------------------------------------

StructInspector::StructInspector(QObject* parent) : QObject(parent) {
    connect(Workspace::instance(), &Workspace::selectionChanged, this, &StructInspector::reload);
    connect(Workspace::instance(), &Workspace::currentChanged, this, &StructInspector::reload);
    reload();
}

void StructInspector::reload() {
    m_structs.clear();
    m_editor.reset();
    m_current = -1;
    m_reason.clear();
    Workspace* w = Workspace::instance();
    if (!w->tree()) m_reason = "Open a disk or an image to inspect its structures.";
    else if (w->selectedMetadataIndex() >= 0) {
        m_structs = core::structuresFor(*w->tree(), {});
    } else {
        m_structs = core::structuresFor(*w->tree(), w->selectedPath());
        if (m_structs.empty()) m_reason = "This node carries no structure the library parses.";
    }
    if (!m_structs.empty()) {
        m_current = 0;
        // A metadata row picks the matching table structure when it can.
        if (w->selectedMetadataIndex() >= 0 && w->tree()->table) {
            const auto regions = w->tree()->table->metadataRegions();
            const auto idx = static_cast<std::size_t>(w->selectedMetadataIndex());
            if (idx < regions.size())
                for (std::size_t i = 0; i < m_structs.size(); ++i)
                    if (regions[idx].contains(m_structs[i].offset)) {
                        m_current = static_cast<int>(i);
                        break;
                    }
        }
        loadCurrent();
    } else {
        refreshViews();
    }
    Q_EMIT changed();
    Q_EMIT selectionChanged();
}

void StructInspector::loadCurrent() {
    m_editor.reset();
    if (m_current < 0 || static_cast<std::size_t>(m_current) >= m_structs.size()) {
        refreshViews();
        return;
    }
    const auto& ref = m_structs[static_cast<std::size_t>(m_current)];
    m_editor = std::make_unique<core::StructEditor>(ref.device, ref);
    if (auto r = m_editor->load(); !r) {
        Workspace::instance()->reportError(r.error());
        m_editor.reset();
    }
    m_fields.setSelected(-1);
    refreshViews();
}

void StructInspector::refreshViews() {
    const stein::ByteCount base = m_editor ? m_editor->current().displayBase : 0;
    m_rows.set(m_editor.get(), base);
    m_fields.set(m_editor ? core::fieldRows(m_editor->current()) : std::vector<core::FieldRow>{}, m_editor.get(), base);
}

QVariantList StructInspector::structs() const {
    QVariantList out;
    for (const auto& s : m_structs) {
        QVariantMap m;
        m["id"] = qs(s.id);
        m["label"] = qs(s.label);
        m["where"] = qs(s.where);
        m["source"] = s.source == core::StructSource::Table ? "table" : "content";
        out.push_back(m);
    }
    return out;
}

void StructInspector::setCurrentStruct(int i) {
    if (i == m_current || i < 0 || static_cast<std::size_t>(i) >= m_structs.size()) return;
    if (m_editor && m_editor->dirtyBytes()) {
        Workspace::instance()->reportError(stein::Error(stein::ErrorCategory::Busy, "write or revert the pending edits before switching structures"));
        return;
    }
    m_current = i;
    loadCurrent();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
}

QString StructInspector::label() const { return m_editor ? qs(m_editor->current().label) : QString(); }
QString StructInspector::where() const { return m_editor ? qs(m_editor->current().where) : QString(); }
QString StructInspector::validity() const { return m_editor ? qs(m_editor->current().validity) : QString(); }

qulonglong StructInspector::selectionStart() const {
    const int i = m_fields.selected();
    if (i < 0 || !m_editor) return 0;
    const auto& f = m_fields.rows()[static_cast<std::size_t>(i)];
    return static_cast<qulonglong>(m_editor->current().displayBase + f.offset);
}

qulonglong StructInspector::selectionEnd() const {
    const int i = m_fields.selected();
    if (i < 0 || !m_editor) return 0;
    const auto& f = m_fields.rows()[static_cast<std::size_t>(i)];
    return static_cast<qulonglong>(m_editor->current().displayBase + f.offset + f.size);
}

QString StructInspector::selectedFieldLine() const {
    const int i = m_fields.selected();
    if (i < 0 || !m_editor) return "no field selected";
    const auto& f = m_fields.rows()[static_cast<std::size_t>(i)];
    QString s = qs(f.name) + " · " + qs(f.typeName) + " · @0x" + QString::number(static_cast<qulonglong>(m_editor->current().displayBase + f.offset), 16).toUpper() + " · " +
                QString::number(f.size) + " B";
    if (!f.isStruct) s += " · " + qs(f.value);
    if (!f.pretty.empty() && f.pretty != f.value) s += " (" + qs(f.pretty) + ")";
    if (!f.message.empty()) s += " · " + qs(f.message);
    return s;
}

int StructInspector::dirtyCount() const { return m_editor ? static_cast<int>(m_editor->dirtyBytes()) : 0; }

QString StructInspector::dirtyRanges() const {
    if (!m_editor) return {};
    QStringList parts;
    for (const auto& r : m_editor->dirtyRegions()) parts << qs(core::hexRangeText({m_editor->current().displayBase + r.offset, r.length}));
    return parts.join(" · ");
}

bool StructInspector::canWrite() const {
    const Workspace* w = Workspace::instance();
    if (!m_editor || !w->current()) return false;
    // Writable targets: raw image files and disks (the latter need elevation); containers never.
    const auto& d = w->current()->descriptor;
    if (d.kind == core::SourceKind::Disk) return w->elevated();
    return d.image && d.image->format == stein::image::VdiskFormat::Raw && d.image->segments == 1;
}

void StructInspector::selectField(int row) {
    m_fields.setSelected(row);
    Q_EMIT selectionChanged();
}

void StructInspector::selectByte(qulonglong displayOffset) {
    if (!m_editor) return;
    const stein::ByteCount off = displayOffset - m_editor->current().displayBase;
    auto i = core::fieldAt(m_fields.rows(), off);
    m_fields.setSelected(i ? static_cast<int>(*i) : -1);
    Q_EMIT selectionChanged();
}

QVariantMap StructInspector::editField(int row, const QString& text) {
    if (!m_editor || row < 0 || static_cast<std::size_t>(row) >= m_fields.rows().size()) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no field"));
    const core::FieldRow field = m_fields.rows()[static_cast<std::size_t>(row)];
    if (auto r = m_editor->setField(field, ss(text)); !r) return errorToVariant(r.error());
    refreshViews();
    m_fields.setSelected(row);
    Q_EMIT changed();
    Q_EMIT selectionChanged();
    return {};
}

namespace {
// "mismatch; computed 0x9CC746FE" -> "0x9CC746FE"
QString computedValue(const std::string& message) {
    const QString m = qs(message);
    const int at = m.indexOf("computed 0x");
    if (at < 0) return {};
    QString hex = "0x";
    for (int i = at + 11; i < m.size() && QChar(m[i]).isLetterOrNumber(); ++i) hex += m[i];
    return hex.size() > 2 ? hex : QString();
}
} // namespace

int StructInspector::fixableChecksums() const {
    int n = 0;
    for (const auto& f : m_fields.rows())
        if (f.type == stein::layout::FieldType::Crc32 && !computedValue(f.message).isEmpty()) ++n;
    return n;
}

int StructInspector::fixChecksums() {
    if (!m_editor) return 0;
    int fixed = 0;
    // Each fix re-describes the structure; outer checksums cover inner ones, so loop.
    for (int pass = 0; pass < 8; ++pass) {
        bool any = false;
        for (std::size_t i = 0; i < m_fields.rows().size(); ++i) {
            const core::FieldRow f = m_fields.rows()[i];
            if (f.type != stein::layout::FieldType::Crc32) continue;
            const QString value = computedValue(f.message);
            if (value.isEmpty()) continue;
            if (auto r = m_editor->setField(f, ss(value)); !r) {
                Workspace::instance()->reportError(r.error());
                break;
            }
            refreshViews();
            ++fixed;
            any = true;
            break;   // rows were rebuilt; start over
        }
        if (!any) break;
    }
    Q_EMIT changed();
    Q_EMIT selectionChanged();
    return fixed;
}

void StructInspector::revert() {
    if (!m_editor) return;
    m_editor->revert();
    refreshViews();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
}

QVariantMap StructInspector::write() {
    Workspace* w = Workspace::instance();
    if (!m_editor || !w->current()) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "nothing to write"));
    if (JobRunner::instance()->running()) return errorToVariant(stein::Error(stein::ErrorCategory::Busy, "an operation is running"));
    core::OpenOptions o;
    o.writable = true;
    auto dev = core::openDevice(w->current()->descriptor, o);
    if (!dev) return errorToVariant(dev.error());
    // The struct's device may be a slice of the root; map the edited bytes through its extents.
    const auto& ref = m_editor->current();
    std::shared_ptr<stein::BlockDevice> target = *dev;
    if (ref.device != w->current()->device) {
        const auto extents = ref.device->extentsOnParent();
        if (extents.size() != 1 || ref.device->parent() != w->current()->device)
            return errorToVariant(stein::Error(stein::ErrorCategory::Unsupported, "edits inside containers or volumes cannot be written back yet"));
        auto slice = stein::SliceDevice::create(target, extents.front(), ref.device->name());
        if (!slice) return errorToVariant(slice.error());
        target = *slice;
    }
    if (auto r = m_editor->commitTo(*target); !r) return errorToVariant(r.error());
    refreshViews();
    Q_EMIT changed();
    Q_EMIT written();
    w->reprobe();
    return {};
}

QVariantMap StructInspector::saveHeader(const QUrl& file) {
    if (!m_editor) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no structure"));
    if (auto r = m_editor->saveTo(pathOf(file)); !r) return errorToVariant(r.error());
    return {};
}

QVariantMap StructInspector::restoreFromFile(const QUrl& file) {
    if (!m_editor) return errorToVariant(stein::Error(stein::ErrorCategory::InvalidArgument, "no structure"));
    if (auto r = m_editor->restoreFrom(pathOf(file)); !r) return errorToVariant(r.error());
    refreshViews();
    Q_EMIT changed();
    Q_EMIT selectionChanged();
    return {};
}

} // namespace drstein::ui
