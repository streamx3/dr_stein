# Dr Stein application architecture

*Status: draft 1 (2026-10-07), written before any UI code. Everything in
`src/` is reviewed against this document; deviations are recorded here, not
left implicit.*

Dr Stein is the desktop front-end of [libstein](../../libstein). The library
owns every byte-level decision (what is on a disk, what an operation would do,
whether it is destructive). The application owns three things only:

1. **Session state**: which disks and images are open, which node is selected,
   which passphrases were typed, what is pending in an operation stack.
2. **Execution**: running the library's long operations on a worker thread with
   progress, cancellation and a report the user can read afterwards.
3. **Presentation**: turning the library's objects into rows, bars, badges and
   sentences, and turning clicks into library calls.

The toolkit decision is Qt 6 Quick/QML with C++ models (libstein decision D17,
`libstein/doc/design/20-gui-toolkit.md`). libstein stays Qt-free (D3).

## 1. Layering

```
 qml/                 Views, components, Theme singleton. No logic beyond
                      "which view is visible" and visual state.
        │  QML bindings, signals
 src/qt/              drstein_qt (static, links Qt6::Core/Qml/Quick).
                      QObject facades and QAbstractItemModels over the core.
                      Threading lives here: one JobRunner, Qt signals to the UI.
        │  plain C++ calls, std::function callbacks
 src/core/            drstein_core (static, Qt-free, C++23, doctest-tested).
                      Session objects, services and presentation helpers over
                      libstein. Synchronous; never spawns a thread except the
                      mount manager (a Mount::run() loop needs one).
        │  libstein public headers
 libstein/            stein::platform, probe, pt, fs, image, ops, app, mount, ...
```

Why a Qt-free core and not QObject models directly over the library: the
core is where every decision that is not pixels lives (how a probe tree
becomes rows, what "largest free region, 1 MiB aligned" means for the Add
Partition form, how an `Error` becomes a sentence and a hint, which bytes a
struct field covers). All of it is unit-testable with libstein's own fixtures
and none of it needs an event loop. The Qt layer then has nothing to get
wrong except marshalling. It also keeps the door open to a second front-end
(the one-button dr_stein app may become a separate, tiny binary over the same
core).

Rules inherited from libstein and kept in the core: no exceptions in our code
(`stein::Expected<T>` everywhere), UTF-8 `std::string`, minimal templates,
single inheritance, "C with classes" readability. The Qt layer uses Qt's
conventions (QString, signals) and converts at the boundary.

## 2. Core: objects

Namespace `drstein::core`. One header per topic under
`src/core/include/drstein/core/`.

### 2.1 Sources (`source.hpp`)

A *source* is something the sidebar lists: a disk the OS knows about or an
image file the user opened.

```cpp
enum class SourceKind { Disk, ImageFile };
enum class SourceGroup { Internal, Removable, Virtual, Images };   // sidebar sections

struct SourceDescriptor {
    std::string id;              // "disk:<identity>" or "image:<absolute path>"
    SourceKind kind;
    SourceGroup group;
    std::string name;            // "nvme0n1", "ws-2026-10-04.stein"
    std::string subtitle;        // "Samsung SSD 980 · NVMe", "/mnt/backup · LZ4 · complete"
    std::string title;           // "Samsung SSD 980" (identity strip)
    std::string path;            // osPath or file path
    std::string identityLine;    // serial · fw · sector sizes · removable...
    ByteCount sizeBytes;
    std::string icon;            // "nvme", "sata", "usb", "sd", "virtual", "image"
    std::optional<platform::DiskInfo> disk;       // kind == Disk
    std::optional<ImageDescriptor> image;          // kind == ImageFile
};

struct ImageDescriptor {
    std::filesystem::path path;
    image::VdiskFormat format;
    std::string variant;         // "v3", "dynamic", "EnCase 6"...
    ByteCount virtualSize;
    bool compressed, encrypted, locked, complete;
    std::size_t segments;
    std::vector<std::string> notes;
    std::string sourceName, sourceIdentity, created;   // .stein manifest
    std::string storedHash;                             // sha256 (.stein) / md5 (E01)
};

Expected<std::vector<SourceDescriptor>> listDisks(platform::Platform& platform);
Expected<SourceDescriptor> describeImageFile(const std::filesystem::path& file, const std::string& passphrase = {});
bool isElevated();
```

### 2.2 Opening and probing (`opened.hpp`)

```cpp
struct OpenOptions {
    bool writable = false;                 // false: ReadOnlyDevice wrapper, always for probing
    std::vector<std::string> passphrases;  // tried on LUKS; VeraCrypt by trial decryption
    std::uint32_t pim = 0;
    std::uint32_t fileSectorSize = 512;
    bool includeMetadataNodes = true;
};

struct OpenedSource {
    SourceDescriptor descriptor;
    std::shared_ptr<BlockDevice> device;   // the root device (read-only unless asked)
    probe::Node tree;                      // the topology
    std::vector<std::string> openNotes;    // container notes ("backing file ignored")
};

// Disk path, .stein, qcow2/VHD/VHDX/VMDK/VDI/E01/DMG, split raw, raw file: the same
// resolution the CLI's openImage() does, then probe.
Expected<OpenedSource> openSource(const SourceDescriptor& source, const OpenOptions& options);
Expected<probe::Node> reprobe(const OpenedSource& opened, const OpenOptions& options);
```

Opening a disk without privileges fails with `Permission`; the UI shows the
disk anyway (its `DiskInfo` comes from IOKit/sysfs without root) and the
error presentation says what to do.

### 2.3 Node addressing and the topology view (`topology.hpp`)

Nodes are addressed by their path of child indices from the root
(`NodePath = std::vector<int>`), which survives model resets as long as the
tree is the same object.

```cpp
using NodePath = std::vector<int>;
const probe::Node* nodeAt(const probe::Node& root, const NodePath& path);

struct TopologyRow {               // one line of the tree table
    NodePath path;
    int depth;
    std::string kindLabel;         // "Device", "Partition 2", "Free", "Decrypted", "Volume", "Metadata"
    std::string name;              // partition name / fs label / device title
    std::string content;           // "FAT32 "EFI" · clean", "LUKS2 → LVM2 → ext4"
    std::string sizeText;
    ByteCount sizeBytes;
    Validity health;
    std::string healthText;        // "OK", "1 warning", "" for free
    int segment;                   // index into segments(), -1 if none
    bool isMetadata;               // hidden unless expert mode
    bool canBrowse, canInspect, canRepair, canMount, hasUsedBar;
    double usedFraction;           // -1 when unknown
};
std::vector<TopologyRow> topologyRows(const probe::Node& root, bool expertMode);

struct Segment {                   // the byte-proportional bar
    NodePath path;
    ByteCount offset, length;
    std::string label;
    bool isFree, isMetadata;
    int colorIndex;                // cycles through Theme's partition palette
};
std::vector<Segment> segments(const probe::Node& root);

struct DetailRow { std::string key, value; bool mono; };
struct NodeDetails {
    std::string kindLabel, title, regionText;
    std::vector<DetailRow> rows;   // type, uuid, content, used, capabilities, cipher, ...
    std::vector<probe::Note> notes;
    std::optional<double> usedFraction;
    std::string usedLabel, usedText;
    std::vector<fs::SubvolumeInfo> subvolumes;
    bool isLockedContainer;        // LUKS/VeraCrypt without a passphrase: offer to unlock
};
NodeDetails nodeDetails(const probe::Node& root, const NodePath& path);
```

### 2.4 Formatting (`format.hpp`)

Decimal sizes for people ("500.1 GB", matching what disks are sold as),
binary where the format is binary ("512 MiB" partitions, "4 KiB" blocks),
exact bytes on demand; LBAs with thin-space grouping; `ErrorCategory` →
`{title, message, hint, severity}` (`Permission` → "needs elevation",
`Busy` → "unmount first", `Integrity` → red); rates, ETAs, timestamps, POSIX
mode strings, GUIDs. Every string a view shows that is not a library string
comes from here, so tests pin them.

### 2.5 Structures and bytes (`structs.hpp`)

The Hex view shows one on-disk structure at a time next to its parsed
fields. The library gives `layout::Node` trees (`PartitionTable::describe()`,
`FileSystem::describe()`); the core lists them, reads their bytes and maps
bytes to fields.

```cpp
struct StructRef {                 // one selectable structure
    std::string id, label;         // "gpt.primary", "GPT header (primary)"
    std::string where;             // "LBA 1 · byte 0x200 · 92 B used"
    ByteCount absOffset;           // on the root device
    ByteCount size;                // bytes shown (rounded up to the sector)
    layout::Node node;             // the parsed tree, offsets absolute on the root device
};
std::vector<StructRef> structuresFor(const probe::Node& root, const NodePath& path);

struct FieldRow {
    std::string name, typeName, value, pretty, doc;
    ByteCount absOffset; std::uint32_t size;
    layout::FieldType type; Validity validity; std::string message;
    int depth; bool editable;      // integers, ascii, bytes ≤ 16: yes; blobs/structs: no
};
std::vector<FieldRow> fieldRows(const StructRef& s);
std::optional<std::size_t> fieldAt(const std::vector<FieldRow>& rows, ByteCount absOffset); // innermost

// Editing happens on an OverlayDevice; nothing reaches the device before commit().
class StructEditor {
public:
    StructEditor(std::shared_ptr<BlockDevice> device, StructRef s);
    Expected<void> load();                         // read the bytes through the overlay
    std::span<const std::byte> bytes() const;
    std::vector<bool> dirtyMask() const;           // per byte
    Expected<void> setField(const FieldRow& f, std::string_view text);   // parse per type, write overlay, re-describe
    Expected<void> setByte(ByteCount absOffset, std::uint8_t value);
    void revert();
    std::size_t dirtyBytes() const;
    Expected<void> commit();                       // device must be writable
    Expected<void> saveTo(const std::filesystem::path&) const;          // the structure as a .sparse piece
    Expected<void> restoreFrom(const std::filesystem::path&);           // into the overlay, then commit() applies
    const StructRef& current() const;              // re-described after every edit
};
```

### 2.6 Files inside a filesystem (`browse.hpp`)

```cpp
struct Entry { fs::DirEntry entry; fs::Stat stat; std::string linkTarget; };
struct Listing { fs::Inode dir; std::vector<Entry> entries; std::string pathText; };

class Browser {                    // one open Reader and a current directory
public:
    static Expected<Browser> open(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options);
    Expected<Listing> list(const fs::Inode& dir);           // sorted: directories first, then by name
    Expected<fs::Inode> enter(std::string_view name);       // relative to the current dir
    Expected<fs::Inode> resolve(std::string_view path);
    std::vector<std::pair<std::string, fs::Inode>> breadcrumbs() const;
    bool caseSensitive() const;
    fs::Reader& reader();
};

enum class PreviewKind { Text, Hex, Image, Empty, TooLarge, Unreadable };
struct Preview { PreviewKind kind; std::vector<std::byte> bytes; std::string text; std::string note; };
Expected<Preview> preview(fs::Reader&, const fs::Inode& file, const fs::Stat& st, ByteCount limit = 256 * KiB);
Expected<std::string> sha256Hex(fs::Reader&, const fs::Inode& file, Progress&);
// copyTree with the UI's defaults; destination directory is where the drop landed.
Expected<fs::CopyTreeStats> copyOut(fs::Reader&, const fs::Inode& source, const std::filesystem::path& destination, Progress&);
```

### 2.7 Imaging requests (`imaging.hpp`)

The Image view's form is a plain struct; `validate()` turns it into library
options or a human error, so the "Start" button can be enabled by the same
function the job uses.

```cpp
struct CreateImageForm {
    std::filesystem::path destination;
    bool raw = false;                       // .img / .raw / .dd or explicit
    image::Compression compression = Lz4;
    std::string chunkSizeText = "4 MiB";
    std::string splitSizeText;              // empty = single file
    image::BadSectorPolicy badSectors = SkipZero;
    bool usedBlocksOnly = true, verifyAfter = true, encrypt = false;
    std::string passphrase, notes;
};
Expected<image::CreateOptions> validate(const CreateImageForm&, const OpenedSource& source, std::vector<std::string>& warnings);

struct RestoreForm { std::filesystem::path image; bool verifyFirst = true, allowSmaller = false; std::string passphrase; };
struct VerifyForm  { std::filesystem::path image; int level = 3; std::string passphrase; };

struct CreateOutcome  { image::CreateResult result; bool verified; image::VerifyResult verify; };
Expected<CreateOutcome>        runCreate(const OpenedSource&, const image::CreateOptions&, bool verifyAfter, Progress&, Report&);
Expected<image::RestoreResult> runRestore(const RestoreForm&, BlockDevice& target, Progress&, Report&);
Expected<image::VerifyResult>  runVerify(const VerifyForm&, Progress&, Report&);
// Key slots of an encrypted .stein: list / add / remove (thin over image::imageKeys & co).
```

### 2.8 Partition editing (`partition_edit.hpp`)

```cpp
class EditSession {                // ops::OperationStack with UI defaults around it
public:
    static Expected<EditSession> open(const SourceDescriptor&, std::uint32_t fileSectorSize);   // opens the device read-write
    ops::OperationStack& stack();
    const probe::Node& base() const;  const probe::Node& preview() const;
    Expected<void> createTable(pt::TableType);
    Expected<void> addPartition(const AddPartitionForm&);      // defaults: largest free region, 1 MiB aligned (CLI rules)
    Expected<void> updatePartition(std::uint32_t index, const EditPartitionForm&);
    Expected<void> deletePartition(std::uint32_t index);
    Expected<void> repairTable();
    Expected<void> wipeSignatures(std::optional<std::uint32_t> index);
    Expected<void> undoLast();  void clear();
    Expected<ops::OperationStack::ApplyResult> apply(Progress&);
};

struct AddPartitionForm { std::string startText, sizeText, endText, typeText, name; std::optional<std::uint32_t> index; bool wipe = true; };
struct PendingRow { int n; std::string title, detail; bool destructive; };
std::vector<PendingRow> pendingRows(const ops::OperationStack&);
struct PreviewRow { std::string name, typeCode, sizeText, change; int colorIndex; bool changed; };
std::vector<PreviewRow> previewRows(const probe::Node& base, const probe::Node& preview);   // "new", "rename", "shrink", "type"
std::string changedRangesText(const ops::OperationStack&);                                 // "0x200–0x400 · ..."
std::vector<PartitionTypeChoice> partitionTypeChoices(pt::TableType);                      // for the type picker
```

### 2.9 Media tests (`media.hpp`)

Wrappers over `ops::surfaceScan` / `ops::capacityTest` that also reduce the
result to a fixed number of cells for the grid (`cellStates(result, n)`:
Ok / Bad / Untested) and sentences for the summary row. The capacity test
is the one place the core enforces a confirmation: `CapacityTestRequest`
carries `confirmText`, and `runCapacityTest` refuses unless it equals the
device's kernel name (image files are exempt). The UI cannot bypass it.

### 2.10 Profiles (`profiles.hpp`)

```cpp
class ProfileStore {               // a directory of *.json profiles
public:
    explicit ProfileStore(std::filesystem::path dir);
    Expected<std::vector<StoredProfile>> list() const;
    Expected<StoredProfile> save(const app::Profile&, std::optional<std::filesystem::path> existing = {});
    Expected<void> remove(const std::filesystem::path&);
};
struct ProfileCard {               // what one card shows, all strings ready
    app::Profile profile; std::filesystem::path file; app::Status status;
    std::string diskText, selectorText, imageText, madeText, fitsText, policyText;
    bool diskPresent, imageReady, canBackup, canRestore, canVerify;
};
ProfileCard profileCard(const StoredProfile&, platform::Platform&);
app::Profile profileFromDisk(const platform::DiskInfo&, const std::filesystem::path& image, std::string name);
// backup / restore / verify / dry-run: thin over app::backup & co, with the RunOptions filled.
```

### 2.11 Mounts (`mounts.hpp`)

`MountManager` keeps every mount the app made (reader → `mount::Mount` plus
its serving thread → mount point under the system temp dir or a user-chosen
one), lists them, unmounts on request and on exit. On macOS this is the NFS
loopback (no privileges), on Linux FUSE, on Windows WinFsp;
`mount::Mount::available()` decides whether the button is enabled.

### 2.12 Progress relay (`progress.hpp`)

```cpp
// A ProgressSink that is safe to call from the worker and hands snapshots to one
// callback. The Qt layer sets a callback that posts to the GUI thread. Tests set a lambda.
class ProgressRelay final : public stein::ProgressSink {
public:
    using OnProgress = std::function<void(const ProgressSnapshot&)>;
    using OnMessage  = std::function<void(std::string)>;
    void onProgress(const ProgressSnapshot&) override; void onMessage(std::string_view) override;
    void setHandlers(OnProgress, OnMessage);
    CancelToken& cancelToken();
};
```

## 3. Qt layer: facades and models

Namespace `drstein::qt`, all types registered to QML through `QML_ELEMENT`
in the module `DrStein`. Each facade wraps exactly the core objects above; no
libstein header is included from QML-facing code except for enums.

| QML type | Kind | Wraps | Notes |
|---|---|---|---|
| `Workspace` | singleton QObject | `listDisks`, `describeImageFile`, `openSource`, `isElevated` | `sources` (SourceListModel with `group` role), `current` (OpenedSourceObject), `refresh()`, `openImage(url)`, `select(id)`, `unlock(passphrase)`; `error(title, message, hint)` signal |
| `SourceListModel` | QAbstractListModel | `SourceDescriptor` | roles: id, group, name, subtitle, sizeText, icon, selected |
| `TopologyModel` | QAbstractListModel | `topologyRows()` | rows filtered by `Settings.expertMode`; `segments` as a QVariantList of maps |
| `NodeDetailsObject` | QObject | `nodeDetails()` | rows, notes, flags, usedFraction |
| `StructInspector` | QObject | `structuresFor`, `StructEditor` | `structs`, `currentStruct`, `HexRowsModel` (16 bytes per row: hex, ascii, per-byte state), `FieldsModel`, `selectField`, `selectByte`, `editField`, `revert`, `write()`, `saveHeader(url)`, `restoreFromFile(url)` |
| `FileBrowser` | QObject | `Browser`, `preview`, `copyOut` | `DirTreeModel` (lazy QAbstractItemModel for the left pane), `EntriesModel` (current directory), `breadcrumbs`, `preview` (text/hex/image), `hash()` (job), `copyOut(entry, dirUrl)` (job), `mount()` |
| `JobRunner` | singleton QObject | `ProgressRelay`, a worker `QThread` | `running`, `title`, `phase`, `fraction`, `doneText/totalText/rateText/etaText`, `messages` (log), `report` (ReportModel), `cancel()`; exactly one job at a time, enforced here |
| `ImageController` | QObject | `imaging.hpp` | form properties bound two-way from QML, `canStart`, `validationMessage`, `start()`, `dryRun()`, `restore()`, `verify()`, `keys` (KeySlotsModel) |
| `PartitionEditor` | QObject | `EditSession` | `pending` (model), `previewRows` (model), `baseSegments`/`previewSegments`, `changedRanges`, `add/update/delete/repair/newTable/wipe`, `undoLast`, `clear`, `apply()` (job); the Apply confirmation dialog is QML but the destructive flag comes from the stack |
| `ToolsController` | QObject | `media.hpp` | `scanCells` (QVariantList), results, `surfaceScan()`, `capacityTest(confirmText, quick, keepPattern)` |
| `ProfilesModel` | QAbstractListModel | `ProfileStore`, `profileCard` | cards; `backup/restore/verify(index, dryRun)` as jobs, `newFromCurrentDisk(imageUrl, name)`, `remove(index)` |
| `MountsModel` | QAbstractListModel | `MountManager` | mount point, what, `unmount(index)` |
| `Settings` | singleton QObject | QSettings | `palette` ("teal"/"blurple"), `colorScheme` ("dark"/"light"/"system"), `expertMode`, `profilesDir`, `recentImages`, `decimalSizes` |

Threading: the GUI thread owns every facade. `JobRunner` moves one
`std::function<Expected<void>(Progress&, Report&)>` to its worker thread,
hands the relay's snapshots back through a queued signal, and delivers the
typed result through a completion callback on the GUI thread. While a job
runs, the facades refuse to start another and the UI disables the buttons
(the library's entities are not thread-safe; one job per process is the
rule, as in the CLI).

Errors: every facade emits `error(title, message, hint, severity)` built by
`core::present(Error)`; the shell shows a toast and, for `Permission`, the
elevation hint.

## 4. QML

`qml/Theme.qml` (singleton) carries the design tokens: two palettes (teal
default, blurple) × dark/light, the 100–900 ramps as arrays, surface/edge
colours, type scale, spacing (0.7× density), radii, and the semantic colours
this app adds to the design system (see `02-ui-translation.md`). Components
in `qml/components/` are written once (button, tag, card, segmented control,
field, check, radio, dialog, icon, key-value grid, fading divider, progress
bar, usage bar). Views in `qml/views/` mirror the mockup's seven screens.
Behaviour comes from Qt Quick Controls (Basic style) templates; the look is
ours.

## 5. Privileges

The app runs unprivileged by default and says so in the top bar. Without
privileges: disks are listed (metadata needs no root on any OS), images do
everything, disks fail to open with a clear `Permission` presentation. The
privileged helper is planned on the library side (`16-platform.md` §3);
until then the documented way to work on real disks is to launch the binary
elevated (`sudo` on Linux/macOS, "Run as administrator" on Windows). Every
destructive action re-reads the device identity into its confirmation
dialog, and the capacity test demands the kernel name typed back.

## 6. Testing

`src/core/tests/` uses libstein's vendored doctest and its fixtures
(`libstein/tests/fixtures/**/*.sparse`, expanded with `SparseFile::loadIntoMemory`):
topology rows for a GPT disk with ext4/FAT/NTFS, struct fields and byte→field
mapping for GPT and FAT headers, field edits round-tripping through the
overlay, the Add Partition defaults, preview diff rows, error presentation,
size formatting, profile cards against a file target, image form
validation. `ctest` runs them; CI mirrors libstein's three platforms.

The Qt layer is exercised by running the app against image files (no
privileges) and, later, by `qmltestrunner` smoke tests of the models.

## 7. Open questions for the owner

1. **Name.** The mockup brands the window "Stein"; the repo is dr_stein.
   This document uses "Dr Stein" for the app and `dr_stein` for the binary.
2. **One-button app.** The Profiles view covers the dr_stein use case
   inside the full tool. Is a separate minimal binary (profiles only) still
   wanted later? The core is split so that it costs little.
3. **Elevation UX.** Relaunch-as-root from inside the app (osascript /
   pkexec / runas) is cheap to add but a blunt instrument; the library's
   helper is the right fix. Which do you want first?
4. **Drag-out.** Drag a file from the viewer to the desktop needs the bytes
   copied to a temporary folder before the drop. Fine for files; for a
   100 GB directory it is not. Proposal: drag-out for entries under a size
   threshold, "Copy out…" with a folder picker for everything.

## 8. Implementation notes (2026-10-07)

What was built against this document, and where it departs from it:

- The core is exactly the module list of §2, under `src/core/`; `drstein_core_tests`
  covers it with libstein's checked-in fixtures (a composite GPT disk with ext4
  and FAT32 is assembled in memory by `tests/fixtures.hpp`).
- The probe never emits `Metadata` nodes; the expert-mode metadata rows are
  synthesized from `PartitionTable::metadataRegions()` in `topologyRows()`.
- `layout::Node` carries no endianness. `StructEditor` infers it per field by
  checking which byte order reproduces the library's printed value.
- The Hex view's "Fix checksums" sets every CRC field the library flags as
  `mismatch; computed 0x…` to that value, innermost first; that is how a
  hand-edited GPT header becomes valid again without the user computing CRCs.
- The Qt facades live in one QML module with the QML files (`src/app`),
  not in a separate `drstein_qt` library: a second `qt_add_qml_module` buys
  nothing for a single executable. `QT_NO_KEYWORDS` is on because libstein has
  a `slots()` method.
- Writable handles exist only while they are needed: `PartitionEditor` opens
  the device read-write when the Partitions view shows and drops it when the
  view changes with nothing pending; the hex writer and the restore job open
  their own handle for the duration of the write.
- One job at a time (`JobRunner`); a probe is a job too, so switching sources
  while something runs is refused with a toast rather than racing the device.
- Mounts are unmounted on `aboutToQuit`; the singleton's destructor never runs.
- `DRSTEIN_VIEW`, `DRSTEIN_SCRIPT`, `DRSTEIN_DELAY`, `DRSTEIN_SCREENSHOT`,
  `DRSTEIN_SCHEME` and `DRSTEIN_PALETTE` exist for scripted runs; the
  screenshots in `screenshots/` were taken that way.
- Open from §7: drag-out (still "Copy out…" with a folder picker), the
  privileged helper (launch elevated for now), a separate one-button binary.

## 9. Partition images (2026-10-07)

The Image view's source is either the whole device or one of its partitions
(`core::sourceScopes`). A partition image is a normal `.stein` whose source is
the partition's `SliceDevice`; the manifest's `source` object says which:

```json
"source": {
  "kind": "partition",
  "name": "Samsung SSD 980 · partition 2 (Data)",
  "identity": "wwn:…/500107862016#2",
  "size": 67108864, "sector_size": 512,
  "partition": { "index": 2, "first_lba": 135168, "last_lba": 266239, "sector_size": 512,
                 "type": "EBD0A0A2-…", "type_name": "Microsoft basic data", "name": "Data", "uuid": "…" },
  "parent": { "name": "Samsung SSD 980", "identity": "wwn:…", "path": "/dev/nvme0n1", "table": "GPT", "size": 500107862016 }
}
```

Whole-device images carry `"kind": "device"`. libstein gained
`CreateOptions::sourceExtra` (an object merged into `source`) for this; the
provenance struct, its JSON and the text form live in `core/imaging.hpp`
(`PartitionProvenance`). `describeImageFile` reads it back, so the sidebar
says "partition · LZ4 · complete" and the identity line shows "partition 2
"Data" of Samsung SSD 980 · LBA 135 168 – 266 239 · Microsoft basic data".

Restore is scope-aware (`core::restoreScopeOf`): a whole-device image lists
disks as targets; a partition image lists the partitions of the source that is
open in the sidebar, each marked same size / larger / too small, and is written
through a `SliceDevice` of that partition so the table and the other partitions
are untouched. Mixing the two is refused with a sentence, not a checkbox.
