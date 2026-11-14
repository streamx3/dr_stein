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
                      mount manager (a Mount::run() loop needs one) and the
                      preview loader (§2.6: a file read must not hold the UI).
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

### 2.6 Files inside a filesystem (`browse.hpp`, `preview_loader.hpp`, `deadline.hpp`, `columns.hpp`)

```cpp
struct Entry { fs::DirEntry entry; fs::Stat stat; std::string linkTarget; std::string sizeText, mtimeText, modeText; };
struct Listing { fs::Inode dir; std::vector<Entry> entries; };   // directories first, then by name (case-insensitive)
struct Crumb { std::string name; fs::Inode inode; };             // "/" for the root

// What Browser::open browses with. A Reader is not thread-safe: whoever reads on another thread opens one of its own.
Expected<std::unique_ptr<fs::Reader>> openReader(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options = {});

class Browser {                    // one open Reader and a current directory
public:
    static Expected<Browser> open(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options = {});
    Expected<Listing> list(const fs::Inode& dir);
    Expected<Listing> listCurrent();
    Expected<void> enter(std::string_view name);            // a directory inside the current one
    Expected<void> enter(std::string_view name, const fs::Inode& listed);   // as listed: two entries of one name told apart
    std::vector<std::string> pathTo(std::string_view name) const;          // names from the root, for another reader
    Expected<void> goTo(std::string_view absolutePath);     // "/etc/ssh"
    std::string up();                                       // both return the name of the child directory that was left,
    std::string jumpTo(std::size_t crumbIndex);             // for the list to re-select; "" when nothing changed
    fs::Inode current() const;
    const std::vector<Crumb>& breadcrumbs() const;
    std::string pathText() const;                           // "/etc/ssh"
    bool caseSensitive() const;
    fs::Reader& reader();
};

// The row a keyboard step lands on: -1 for an empty list; from no selection a step down takes
// the first row and a step up the last; otherwise the step is clamped to the list.
int movedIndex(int current, int count, int delta);

Expected<std::string> sha256Hex(fs::Reader&, const fs::Inode& file, const fs::Stat& st, Progress&);
// Copies `source` (a file, a directory tree or a symlink) to `destinationDir / name`.
Expected<fs::CopyTreeStats> copyOut(fs::Reader&, const fs::Inode& source, const std::string& name, const std::filesystem::path& destinationDir, Progress&);
```

The preview half, copied from the header (hence the `stein::` that the other
blocks drop):

```cpp
enum class PreviewKind { Empty, Text, Hex, Image, Unreadable };
struct Preview {
    PreviewKind kind = PreviewKind::Empty;
    std::vector<std::byte> bytes;  // what was read: a whole image, otherwise the head
    std::string text;              // for Text, and the source of an SVG image; at most PreviewLimits::text
    std::string note;              // "first 256 KiB of 1.2 GiB"
    std::string textNote;          // "first 64 KiB of 300 KiB" when `text` is not the whole file; else empty
    bool truncated = false;
    std::string imageFormat;       // "png", "jpeg", "svg", ...: set whenever the content was recognised
};

struct PreviewLimits {
    stein::ByteCount head = 256 * stein::KiB;   // read first and sniffed; all a text or hex preview shows
    stein::ByteCount image = 64 * stein::MiB;   // a recognised image up to this size is read whole
    stein::ByteCount svg = 512 * stein::KiB;    // an SVG up to this size is read whole and vetted; above it, text
    stein::ByteCount chunk = 1 * stein::MiB;    // read size past the head; cancellation is checked per chunk
    stein::ByteCount text = 64 * stein::KiB;    // the most of a text that is shown: more stalls a text view on every file
};

// What the file is shown as. A recognised image within limits.image comes back whole (Image);
// above it the head is shown as Hex with a note. An SVG is an Image only when it is within
// limits.svg and svgSafeToRender(); otherwise it stays Text. Text shows at most limits.text
// bytes, and the note says so when that is not the whole file. A read failure is a Preview of
// kind Unreadable with the message as its note; the only error returned is Cancelled, when
// `cancelled` turns true before a read.
stein::Expected<Preview> preview(stein::fs::Reader& reader, const stein::fs::Inode& file, const stein::fs::Stat& st, const PreviewLimits& limits = {},
                                 const std::atomic<bool>* cancelled = nullptr);

// The image format the first bytes of a file announce, as a lowercase id: png jpeg gif bmp webp
// tiff ico cur icns heic avif jp2 j2k psd pnm xpm; "" when none. Formats without a magic number
// (TGA) and text formats (SVG) are not found here.
std::string sniffImageFormat(std::span<const std::byte> head);
// "jpeg" -> "JPEG", "jp2" -> "JPEG 2000"; an unknown id comes back in upper case.
std::string imageFormatLabel(std::string_view id);
// Whether an SVG can be handed to a renderer that resolves references: false when it declares
// entities, carries scripts, event attributes, <image> or <foreignObject> (under any namespace
// prefix: the renderer goes by the local name), imports a style
// sheet, moves its base (xml:base), or points anywhere but at one of its own elements (href,
// xlink:href, url()). A hostile file on the browsed disk must not make the renderer open files
// of the host. Conservative: harmless files are refused too when they merely look like any of that.
bool svgSafeToRender(std::string_view svg);
```

What the comments leave out:

- Directories, links, special files and empty files are `Empty` with a note
  ("a directory", "empty file") before the reader is touched; the GUI thread
  relies on that to describe them without a read.
- A short magic number is not trusted alone: BMP, ICO/CUR, ICNS, TIFF, PSD
  and PNM need a plausible header too ("BM" plus garbage is not a bitmap,
  a note that starts with "icns" is not an icon file), and
  the generic HEIF brands are told apart by their compatible brands.
- The note of an image above the limit names the format ("PNG image of
  120 MiB: above the 64 MiB preview limit"). A picture that ends early is
  still an `Image` of what could be read, with a note saying so.
- Text is the first 64 KiB of the head with a cut UTF-8 sequence trimmed,
  three bytes at most (the head is judged as text with that cut trimmed too:
  UTF-8 text with one control character used to show as hex above 256 KiB); the note then reads "first 64 KiB of 600 KiB". The
  whole head (256 KiB) is still read and sniffed. An SVG's source is cut the
  same way, and `textNote` says so for when it is shown as text after all.
- An SVG is text whose first element is `<svg` (a byte order mark, an XML
  declaration, comments and a DOCTYPE may precede it). QtSvg opens the local
  files an SVG names, so `svgSafeToRender()` is the only gate for what an
  SVG may touch (the cost of rendering it is checked in the Qt layer, §10).
  Since 2026-11-14 it also refuses `<?xml-stylesheet` (QtSvg opened
  `href="#name.css"` as a file), reads `url (` with white space before the
  parenthesis as a reference, and refuses an XML declaration that names an
  encoding other than UTF-8, ASCII or Latin-1: the parser decodes by the
  declaration while the gate reads bytes, which a Qt built with ICU could
  be made to read differently.
  Elements are matched by their local name: QtSvg ignores the prefix, so
  `<s:image href="#x/a.png">` with `s` bound to the SVG namespace drew the
  file `#x/a.png` relative to the working directory until 2026-11-14. Illustrator
  files with entities and Inkscape files with an embedded `<image>` are
  refused with the rest and shown as text.

`preview_loader.hpp` takes the read off the caller's thread:

```cpp
struct PreviewResult {
    std::uint64_t ticket = 0;
    stein::Expected<Preview> preview;   // a read failure is a Preview of kind Unreadable; an error is anything else that went wrong
    std::shared_ptr<void> decoded;      // what the transform made of the preview; empty without one or on an error
};

class PreviewLoader {
public:
    // Runs on the worker with no lock held, and may still be running after the loader is gone:
    // it must not touch the loader's owner. It, and what it captured, is destroyed with the
    // worker, on whichever thread lets go last.
    using Transform = std::function<std::shared_ptr<void>(const Preview&)>;
    // Runs on the worker under the loader's lock: it hands the result over (posts it to the
    // owner's thread) and must not call back into the loader. Destroyed by ~PreviewLoader.
    using Deliver = std::function<void(PreviewResult)>;

    PreviewLoader(std::unique_ptr<stein::fs::Reader> reader, PreviewLimits limits, Transform transform, Deliver deliver);
    // Never waits for a read or a transform: the worker is told to stop and left to finish the
    // call it is in; it keeps the reader alive until then. Nothing is delivered once this returned.
    ~PreviewLoader();

    // Supersedes every earlier request: one in progress is cancelled at its next chunk, one
    // waiting is dropped, and neither is delivered. Returns the request's ticket: increasing, never 0.
    std::uint64_t request(PreviewTarget target);   // { path from the root (Browser::pathTo), listed Stat }
    // Supersedes every earlier request without making a new one.
    void cancel();
    PreviewActivity activity() const;              // { ticket being served or 0, Idle | Reading | Transforming }

    static std::size_t workersAlive();             // of every loader, those whose loader is gone included
    static bool waitForWorkers(std::chrono::milliseconds patience);
};
```

One detached worker thread owns a reader and serves only the latest request,
so stepping through a directory never queues up reads. Its guarantees:

1. At most one request is in progress. A newer `request()` or `cancel()`
   raises the cancel flag of that one, so a large read stops at its next
   chunk, and drops the one that waits.
2. `Deliver` is called only for the ticket that is still the latest at that
   moment, at most once per ticket, never for a cancelled one and never
   after `~PreviewLoader` has returned. One mutex covers `request()`,
   `cancel()`, the destructor and the delivery itself.
3. The destructor sets stop, wakes the worker and returns. The worker
   co-owns the state, reader included, through a `shared_ptr` and exits
   after the read or transform it is in. It is never joined: a read on
   failing media may take arbitrarily long.
4. A failed read is delivered (a `Preview` of kind `Unreadable`). A null
   reader, and an exception out of the reader or the transform, are
   delivered as errors.
5. A request names its file by path, not by inode: the worker looks each
   name up in its own reader, then checks that what it found has the
   listed type, size and mtime; otherwise, or when the path leads nowhere,
   the preview is `Unreadable` with the reason. Two readers of one
   filesystem need not number its files alike: btrfs numbers subvolumes in
   the order a reader meets them, so every file inside a subvolume was
   "unreadable" to the worker until the multi-reviewer pass of 2026-11-14.
   On damaged media with two entries of one name, the second is refused
   rather than shown with the first one's content.
6. `activity()` tells the owner whether the worker is reading or
   transforming, and for which ticket, so a stall can be told from a slow
   decode (§10). `workersAlive()` counts every worker not yet returned;
   `waitForWorkers()` lets the process wait for them before its static
   destructors run (a decode inside Qt crashed on quit otherwise).

Why a second reader: a `fs::Reader` is not thread-safe (FAT, exFAT, APFS,
btrfs and others fill caches lazily), while block devices are safe for
concurrent `readAt` and a reader co-owns its devices, so it can outlive the
probe tree. The worker therefore reads through a reader of its own, opened
with `openReader()` for the same node and options, and never touches the
`Browser`'s. Two threads decompressing at once needed one fix in libstein:
the lazily built static tables of `inflate.cpp` and `bzip2.cpp` are now a
function-local static and a compile-time constant.

What a superseded request cannot stop is the call the worker is in: one
`read()` call (a chunk at most) or a transform (a decode). The next request
waits behind it. What the app builds on this (decoders, timing, the
keyboard) is in §10.

`deadline.hpp` runs a call that cannot be interrupted against a deadline
(added 2026-11-14 for drawing SVGs, §10):

```cpp
class DeadlineRunner {
public:
    enum class Outcome : std::uint8_t { Finished, TimedOut, Busy };
    explicit DeadlineRunner(std::size_t places);   // calls at once, the abandoned ones included; at least 1
    ~DeadlineRunner();                             // never waits for a call
    Outcome run(std::function<void()> call, std::chrono::milliseconds deadline);
    std::size_t running() const;
    bool waitUntilIdle(std::chrono::milliseconds patience) const;
};
```

Each call runs on a thread of its own, and `run()` waits for it at most the
deadline, counted from the call: waiting for a place takes from it. A call
that never got a place is `Busy` and never starts. A call still running at
the deadline is `TimedOut` and abandoned, not stopped: it runs to its end,
keeps its place until then, and is destroyed on its own thread before it
gives the place back, so `waitUntilIdle()` finding none means none of what
the calls captured is left either. The call must own what it touches (it may
outlive the caller and the runner); the caller reads its result only after
`Finished`. An exception out of a call that finished in time is rethrown by
`run()`; one out of an abandoned call is dropped. Stopping a call for real
would need a separate process: `doc/design/08-svg-out-of-process.md`
(postponed).

`columns.hpp` holds the decisions of Browse's column layout (§11), as
Finder shows a folder: one column per breadcrumb, the last one current, and
a peek into the folder selected there.

```cpp
struct ColumnPlan {
    fs::Inode dir;                 // the directory the column lists
    std::string title;             // the crumb's name ("/" for the root); the folder's name for the peek
    std::size_t crumb = 0;         // the column's position: crumbs.size() for the peek
    bool current = false;          // the last breadcrumb: the list with the selection
    bool peek = false;             // the folder selected in the current column, nothing selected in it
    std::string selectName;        // an ancestor's row shown selected (the next crumb's name); empty otherwise
    bool operator==(const ColumnPlan&) const = default;
};
// Column k lists breadcrumb k; a selected directory in the current column adds a peek column.
std::vector<ColumnPlan> planColumns(const std::vector<Crumb>& crumbs, const Entry* selectedInCurrent);
// How many leading columns list the same directories before and after a step: those keep their views.
std::size_t keptColumns(const std::vector<ColumnPlan>& before, const std::vector<ColumnPlan>& after);
int rowOfName(const Listing& listing, std::string_view name, bool caseSensitive);   // -1 when absent

enum class ColumnClick { SelectInCurrent, JumpThenSelect, EnterThenSelect, Ignore };
ColumnClick classifyClick(std::size_t column, const std::vector<ColumnPlan>& plans);

class ListingCache {               // listings of directories by inode; the least recently used goes past capacity
public:
    explicit ListingCache(std::size_t capacity = 64);
    std::shared_ptr<const Listing> find(const fs::Inode& dir);    // a hit counts as a use; null on a miss
    void put(std::shared_ptr<const Listing> listing);            // replaces one of the same directory
    Expected<std::shared_ptr<const Listing>> get(Browser& browser, const fs::Inode& dir);   // reads on a miss
    void clear();
};
```

What the comments leave out:

- A peek is added for an entry whose `stat` says directory, whatever type
  its directory entry reports: a symbolic link to a folder gets none.
- `keptColumns` compares directories by inode, not by name, so a folder
  replaced by another of the same name is not kept. The facade relies on the
  inodes of the breadcrumbs (from `lookup`) equalling those of the listings
  (from `readdir`); that was checked on ext4 and exFAT.
- `rowOfName` takes an exact match first. On a filesystem that ignores case
  it then folds ASCII letters only, without the locale: a library `tolower`
  may fold the bytes of UTF-8 sequences as if they were Latin-1. A path
  typed in another case with non-ASCII letters (`goTo` on exFAT) therefore
  leaves its ancestor column without a selected row; clicks and keys always
  carry the name as it is on disk.
- `ListingCache` is keyed by the inode alone and bounded by directories, not
  entries. A read error is returned and not cached, so the next `get` reads
  again. The filesystem is read-only, so a listing never goes stale while
  the same `Browser` reads it; whoever opens another one clears the cache.

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
| `FileBrowser` | QObject | `Browser`, `PreviewLoader`, `ListingCache`, `copyOut` | `DirTreeModel` (lazy QAbstractItemModel for the list layout's tree), `EntriesModel` (current directory; it shares its listing, does not copy it, and has `get(row)`), `columns` (a `ColumnsModel`, empty in the list layout), `breadcrumbs`; `select(row)` for a click, `moveSelection(delta)` / `selectFirst()` / `selectLast()` / `activateSelected()` for the keyboard, `up()` and `jumpTo(crumb)` re-select the directory that was left; in the column layout `columnClicked(column, row)`, `columnActivated(column, row)` (a double-click), `stageColumnForDrag(column, row)` and `enterSelectedAndSelectFirst()` (§11); the preview as `previewKind` ("none", "off", "loading", "empty", "text", "hex", "image", "unreadable"), `previewLoading`, `previewText`, `previewHex`, `previewImage` (a QImage), `previewNote`, `previewEnabled`, `previewOnce()` (§10); `hash()` (job), `copyOut(entry, dirUrl)` (job), `prepareCopy(row)` + `copyPrepared(dirUrl)` (Copy out…: the entry is fixed when the folder dialog opens, and nothing is copied, with a toast, if the disk was read again meanwhile), `stageForDrag(rows)`, `mount()` |
| `ColumnsModel` | QAbstractListModel | `planColumns`, `keptColumns`, `rowOfName` | one row per column, left to right; roles: `title` (the volume's title for the root), `current`, `peek`, `crumb`, `selectedRow` (the selection in the current column, the row on the path in an ancestor, -1 in the peek), `entries` (an `EntriesModel` of the column's own), `entryCount`, `error` (why the directory could not be listed); `count`, `get(row)`. A step replaces only the columns whose directory changed (§11) |
| `PreviewSurface` | QQuickPaintedItem | a `QImage` | `image`; painted centred and fitted, at most one image pixel per screen pixel, so a decoded preview needs no URL or image provider |
| `JobRunner` | singleton QObject | `ProgressRelay`, a worker `QThread` | `running`, `title`, `phase`, `fraction`, `doneText/totalText/rateText/etaText`, `messages` (log), `report` (ReportModel), `cancel()`; exactly one job at a time, enforced here |
| `ImageController` | QObject | `imaging.hpp` | form properties bound two-way from QML, `canStart`, `validationMessage`, `start()`, `dryRun()`, `restore()`, `verify()`, `keys` (KeySlotsModel) |
| `PartitionEditor` | QObject | `EditSession` | `pending` (model), `previewRows` (model), `baseSegments`/`previewSegments`, `changedRanges`, `add/update/delete/repair/newTable/wipe`, `undoLast`, `clear`, `apply()` (job); the Apply confirmation dialog is QML but the destructive flag comes from the stack |
| `ToolsController` | QObject | `media.hpp` | `scanCells` (QVariantList), results, `surfaceScan()`, `capacityTest(confirmText, quick, keepPattern)` |
| `ProfilesModel` | QAbstractListModel | `ProfileStore`, `profileCard` | cards; `backup/restore/verify(index, dryRun)` as jobs, `newFromCurrentDisk(imageUrl, name)`, `remove(index)` |
| `MountsModel` | QAbstractListModel | `MountManager` | mount point, what, `unmount(index)` |
| `Settings` | singleton QObject | QSettings | `palette` ("teal"/"blurple"), `colorScheme` ("dark"/"light"/"system"), `expertMode`, `profilesDir`, `recentImages`, `decimalSizes`, `previewFiles` (default on; `DRSTEIN_PREVIEW=0\|1` decides it for one run, and such a run never writes it), `browseLayout` ("columns", the default, or "list"; any other value reads as "columns"; `DRSTEIN_BROWSE_LAYOUT=columns\|list` decides it for one run, and such a run writes neither it nor the three widths), `browseTreeWidth` (200, kept within 150–420), `browsePreviewWidth` (280, 220–560), `browseColumnWidth` (240, 140–640), each written when a drag ends, the column width also by a fit |

Threading: the GUI thread owns every facade. `JobRunner` moves one
`std::function<Expected<void>(Progress&, Report&)>` to its worker thread,
hands the relay's snapshots back through a queued signal, and delivers the
typed result through a completion callback on the GUI thread. While a job
runs, the facades refuse to start another and the UI disables the buttons
(the library's entities are not thread-safe; one job per process is the
rule, as in the CLI). `FileBrowser`'s preview worker is a thread beside the
job's: it reads and decodes through a reader of its own (§2.6) and posts
what it made to the GUI thread with a queued call; no new preview is asked
for while a job has the UI locked (§10). Directories are listed on the GUI
thread through the `Browser`; the columns' are not read while one of the
view's own jobs reads through it, and catch up when it ends (§11).

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
`test_preview.cpp` covers the preview half of §2.6: the sniffing table with
its near misses, the limits, cancellation between chunks, safe and unsafe
SVGs on files served from memory (`mount::makeSingleFileReader`), and the
loader's guarantees with a reader that blocks until the test lets it go.
It synchronises with condition variables; timeouts are failure bounds only.
`test_deadline.cpp` covers `deadline.hpp`: a call in time, a late one given
up at its deadline that runs on and lets go of what it captured before its
place is free, a call that waits for a place and one that finds none (and
never starts), exceptions in time and too late, a runner destroyed before
its abandoned call, and a bound of 0 meaning 1. Calls are held at a gate
the test opens, so only the deadlines themselves are timed.
`test_columns.cpp` covers `columns.hpp`: the plans for the root, a deep path
with Unicode names, a selected file (no peek) and a selected folder (a
peek), the peek following `stat` rather than the directory entry's type;
which columns survive a step (another inode under the same name is not
kept); `rowOfName` exact first, then folded; every kind of click and an
out-of-range one; and the cache (one read per directory, least-recently-used
order and eviction, replacement, a capacity of 0 holding one, a read error
not cached) through a counting, failure-injecting in-memory reader wrapped
in `Browser::fromReader`. A path on the ext4 fixture and one typed in another
case on the exFAT fixture are planned, found and cached for real.

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

## 10. Browse: previews and the keyboard (2026-11-14)

Selecting an entry shows its name, details and times at once and reads no
file content on the GUI thread. The content comes from a
`core::PreviewLoader` (§2.6) that `FileBrowser` creates on first need, with a
second reader from `core::openReader()` for the same node and
`ReaderOptions` as its `Browser`. That reader is opened on the GUI thread,
as the `Browser`'s is; when it cannot be opened, files show as "unreadable"
with the reason.

- **When it asks.** A click asks at once. Keyboard movement asks after an
  80 ms pause, so a held arrow key skims the list without a read per row.
  No request starts while a job has the UI locked (`Workspace.uiLocked`: the
  job may be writing under the reader), and one in flight when the lock
  comes is cancelled. The card then says "waiting for the running job" and
  the selection is asked for again when the lock ends.
- **What crosses threads.** The loader's transform (`renderPreview`, a free
  function that captures nothing of the `FileBrowser`) runs
  `decodePreview()` and builds the strings on the worker. The file's bytes
  end there: the picture, the note, the text and a 4 KiB hex head are posted
  to the GUI thread with a queued call, and a result whose loader or ticket
  is no longer the current one is dropped.
- **While it loads.** The previous content stays up for 150 ms, dimmed
  (`previewLoading` is true), then the kind turns "loading". The 12 most
  recent previews, 96 MiB at most, are kept by inode, size and mtime, so a
  step back is instant; failed reads are not kept.
- **When the loader goes.** With its reader: whenever the `Browser` is
  reopened (a reload, another subvolume), when previews are switched off,
  when the selection moves on after "Preview this file" with previews off,
  and with the `FileBrowser`. A reload asked for while one of the view's own
  jobs (hash, copy out) reads through the `Browser` waits for that job.
- **A stuck worker.** A request that has waited 10 s gets a new loader only
  when `activity()` says the worker is still in the *transform* of a file
  given up for it, and fewer than three workers are alive in the process.
  The old worker finishes its decode on its own and delivers nothing. A
  worker stuck in a *read* is waited out: every real block device
  serialises its reads, so a new reader, opened on the GUI thread, would
  have frozen the UI behind the same read. A slow decode of the file asked
  for is not started over.
- **On quit.** `main()` destroys the QML objects (each loader tells its
  worker to stop), waits up to 1 s for the workers, and if one is still in
  a decode ends the process with `_Exit` instead of running static
  destructors under it: quitting during an SVG render crashed 2 runs in 4
  before. An SVG drawing that was given up (below) may run on for minutes,
  so it gets 250 ms more, and then the same `_Exit`.
- **Off.** `Settings.previewFiles` is the "auto" check on the preview card
  and "Preview files in Browse" in Settings; `DRSTEIN_PREVIEW=0|1` decides
  it for one run, and the elevated relaunch passes it on. Switching it off
  closes the loader and its reader. A selected file then shows kind "off"
  and is read only by `previewOnce()`, the card's "Preview this file".

`decodePreview()` (`src/qt/preview_decode.hpp`) turns an `Image` preview into
a `QImage` that fits 2048 × 2048, upright and never enlarged (an SVG smaller
than 512 px is drawn at 512). It tries, in order:

0. **Core first** refuses, by its headers, a JPEG whose decoder would hold
   every DCT coefficient of the picture: a progressive one, or one in
   several scans, above 256 MiB of coefficients ("progressive JPEG image,
   16384 × 16384: too large to decode"; a 171-byte file made libjpeg hold
   2 GiB), and one of more than 100 scans at any size (each scan walks all
   coefficients again: 10 000 scans kept the worker busy for minutes). It
   is shown as hex with that note.
1. **Qt**: `QImageReader` over the bytes, the format decided from the
   content. Baseline JPEG and SVG decode straight to the fitted size. Other formats
   decode at full size and are scaled afterwards, and only when the full
   size stays under 256 MiB, `QImageReader`'s own limit: fewer than
   8192 × 8192 pixels. An SVG is rendered only when core passed the id
   "svg": that id says the text went through `svgSafeToRender()`. Before
   QtSvg sees it, `QXmlStreamReader` counts what it would draw: each element
   once, plus what every `<use>` and every `url(#id)` in an attribute (mask,
   clip path, marker, pattern, filter; not a gradient) draws again,
   memoised per element. It is shown as text, with the reason as its note,
   when that comes to more than 100 000 elements, when references loop, or
   when nesting and references together go more than 128 levels deep:
   QtSvg draws both by recursion, and a chain of 500 `<use>` overflowed the
   worker's 512 KiB stack on macOS (300 did not). Ten levels of ten `<use>`
   (2.4 KiB) used to keep the worker busy for minutes; it is now turned down
   in well under a millisecond. Since the review of 2026-11-14 the count also
   follows `url (#id)` with white space before the parenthesis (QtSvg does),
   counts a reference to an id defined twice at every definition (decoys at
   the end hid a 600-link chain that crashed the worker), follows the
   references in `<style>` rules to the elements a plain selector (`rect`,
   `.a`, `#b`, `rect.a`, `*`) matches and takes any other selector to match
   all, refuses a style sheet whose rules times elements exceed 10 million,
   adds 2000 for each mask or filter applied (each is a layer the size of
   the picture: 125 nested masks took 4 GiB) and draws a marker once per
   point of its path. An SVG is drawn at 1024 px at most: the pane is 560
   points wide, and fills, masks and blurs cost by the area.
   Since 2026-11-14 an SVG above 512 KiB (it was 1 MiB) is not read whole
   and is shown as text, and one that passes everything above is drawn
   through a `core::DeadlineRunner` (§2.6), on a thread of its own with a
   copy of its source, and given up when it has not drawn within 2 s: it is
   shown as text, "SVG image that did not draw within 2 s", and that preview
   is remembered like any other, so coming back to the file does not draw it
   again. A drawing given up cannot be stopped: it runs on unseen and keeps a
   core busy until it ends. At most two SVGs draw at once, the given-up ones
   included, so at most two cores; past that, an SVG waits for a place within
   its own 2 s, or is shown as text with "earlier ones are still drawing",
   which is not remembered (`DecodedPreview::retry`): coming back tries again.
   The drawing thread is a `std::thread` like the worker, with the same
   512 KiB stack on macOS for which the depth limit was measured.
2. **The platform decoder**, for what Qt has no decoder for, cannot read or
   finds too large: ImageIO on macOS (`preview_decode_mac.cpp`). It makes
   the thumbnail without holding the whole picture only for baseline JPEG,
   PNG that is not interlaced and HEIF/AVIF (a 16384 × 16384 one stays near
   140 MiB). Anything else, and any floating-point picture, is refused when
   the whole of it at its own depth would take more than 512 MiB: an
   interlaced PNG of 16384 × 16384 took 1 GiB, a float TIFF 8 GiB. That
   turns down some large TIFFs ImageIO would have streamed. There is none
   on Linux and Windows.
3. **Neither**: no picture, and a sentence saying why ("HEIC image: this
   build has no decoder for it", "PNG image, 12000 × 9000: too large to
   decode", "not a readable BMP image"). The card shows the hex head with
   that sentence as its note; for an SVG, its source (its first 64 KiB).

A raster picture above 16384 × 16384 pixels is refused on both paths,
because a decode cannot be interrupted.

| Decoder | Pictures it shows |
|---|---|
| Qt alone, on every platform | PNG, JPEG, GIF (first frame), BMP, ICO and CUR (the largest entry), PBM/PGM/PPM, XPM, SVG |
| ImageIO adds, on macOS | TIFF, WebP, HEIC, AVIF, JPEG 2000, ICNS; PNG, BMP and the like from 8192 × 8192 pixels up |
| Linux and Windows | what the installed Qt image plugins read: Qt's Image Formats add-on brings TIFF, WebP and ICNS; HEIC has no decoder there, and neither have AVIF and JPEG 2000 in Qt's own builds; a picture other than a JPEG from 8192 × 8192 pixels up is "too large to decode" |

**What the card shows.** `PreviewSurface` lays a picture with an alpha
channel on a checkerboard of 8 px squares in two neutral steps of the theme
(`checkerColor`, `checkerAltColor`), so black drawn on nothing shows in the
dark scheme too. Text is at most 64 KiB (a larger `TextEdit` stalled the UI
about 200 ms on each keyboard pause in a debug build). The selected row is
tinted with the accent and carries a 3 px accent bar while the list has the
keyboard; while it does not, both turn grey. The line under the panes says
what is read when: "a file when it is selected" with previews on, "a file
only when you ask" with them off.

**Keys.** The entries list holds the keyboard, or in the column layout the
column strip (§11). Whichever is shown takes focus when a row or its empty
space is clicked, when Browse becomes the current view, when the layout
setting changes and when the view is enabled again after a job, but never
from an open dialog. The `ListView`s' own key navigation is off, because the
selection lives in the C++ model; a key goes up from the focused list or
strip to a `Keys.onPressed` on its pane in `BrowseView.qml`, which calls
`keyPressed(key, modifiers, autoRepeat)` on the view's root item. A
`DRSTEIN_SCRIPT` can call that too (`browseView.keyPressed(...)`,
`moveSelection(delta)`, `pageRows()`).

| Key | Does |
|---|---|
| Up, Down | one entry up or down |
| PageUp, PageDown | a page: the fully visible rows less one |
| Home, End | the first, the last entry |
| Return, Enter, Right | enters the selected directory: in the list with nothing selected, in columns with its first entry selected, as Finder does; nothing on a file |
| Backspace, Left | the parent directory, with the directory that was left selected (in columns it is the peek again); nothing at the root |

The selected row is kept in view (`positionViewAtIndex(row,
ListView.Contain)`). Keys with Ctrl, Alt or Meta are not taken. The four
directory keys ignore auto-repeat, so a held key does not walk through
directories. Nothing moves while a job has the view disabled or another
view is shown. The line under the panes names the keys of the layout shown.

Not covered:

- **Formats without a decoder on the platform** show as hex with the note
  of step 3: on Linux and Windows that is every format outside the table's
  first row, unless a Qt plugin for it is installed. TGA has no magic number
  and is hex everywhere. PSD and bare JPEG 2000 codestreams are recognised
  but were not tried. Camera raw files carry TIFF's magic and go to whatever
  reads TIFF.
- **Audio and video** are not previewed; they show as hex.
- **Animation and orientation.** A GIF shows its first frame. A PNG's eXIf
  orientation is ignored; JPEG, TIFF and HEIC are turned upright.
- **Interrupting a decode.** A file selected right after a very large
  picture waits behind it (about 1.2 s after a 12000 × 9000 PNG in a debug
  build). An SVG within the limits can still be slow by sheer drawing, and
  no count bounds that: one self-crossing path of 90 000 segments with an
  even-odd fill took 251 s, a dashed path of 60 000 segments over 300 s,
  12 000 gradient-filled rectangles about 7 s at 1024 px. The 512 KiB limit
  turns those three files away, but not much smaller ones (release Qt on
  this Mac, no limit: a self-crossing path of 5 000 segments, 48.5 KiB, 10 s;
  of 45 000, 436 KiB, 61 s; a dashed path of 45 000 segments, 394 KiB, over
  90 s; 8 000 gradient rectangles, 383 KiB, 4.6 s; ten full-picture blurs,
  851 bytes, 2.6 s). The 2 s limit bounds the wait for a preview, not the
  work: a drawing given up runs to its end on its own thread, two at most.
  Stopping it needs a separate process, designed and postponed in
  `doc/design/08-svg-out-of-process.md`. The 10 s rule above no longer
  concerns SVGs, whose transform returns within about 2 s.
- **Tests.** The core half is tested (§6). `src/qt` and the QML have no
  test in the repository: the decoders, `FileBrowser` and the view were
  checked with scratch programs and scripted runs on macOS. The key handler
  was driven, in both layouts, through `browseView.keyPressed()` and by
  synthetic key events; no test presses a real key, so the way from the
  keyboard to the handler is left to a check by hand. The SVG count
  (`svgCostRefusal`) has no test of its own; the owner deferred those tests.
  The review of 2026-11-14 checked it against the reviewers' crafted files
  with a scratch program (`doc/notes/2026-11-14-browse-preview-status.md`
  §13). The 2 s limit and the two places are `core::DeadlineRunner`'s, which
  `test_deadline.cpp` covers; the SVG half was checked with a scratch
  program and a scripted run (status note §14).
  None of this has been built or run on Linux or Windows.

## 11. Browse: the column view and the panes (2026-11-14)

The owner asked for names that fit and for a column view as in Finder, as
the default, with the choice in Settings. `Settings.browseLayout` is
"columns" (the default) or "list"; the Settings dialog has "Browse layout"
(Columns | List) just above "Preview files in Browse".
`DRSTEIN_BROWSE_LAYOUT=columns|list` decides it for one run, and such a run
stores neither the layout nor the pane widths below. The elevated relaunch
passes it on, so the elevated copy stores none of them either. The QML is
`BrowseView.qml` (the panes, the keys, the preview card),
`BrowseColumns.qml` (the strip) and `BrowseList.qml` (the table).

**The columns.** `core::planColumns` (§2.6) over the breadcrumbs: column k
lists breadcrumb k, from the root on, and the last one is the current
column. That is the current directory with its selection, preview, keys and
jobs, exactly as in §10. In each ancestor the row of the next breadcrumb
shows as selected, always in the grey style; the current column's selection
has the accent while the strip holds the keyboard and is grey while it does
not. When the current selection is a directory, a peek column to its right
lists it with nothing selected; a file selected has no peek, and the preview
card shows it. The peek follows a click at once and a key step after the
preview's 80 ms pause (a timer of its own), so a held arrow key reads no
folders. Rows show an icon, the name (elided in the middle, so the extension
stays) and a chevron for folders; there are no column titles, as in Finder.
An empty folder says "empty folder"; a folder that cannot be listed shows
why in its column. A click tries such a folder once more, a key step does
not.

**What survives a step.** `ColumnsModel` replaces only what changed:
`keptColumns` counts the leading columns that list the same directories
before and after, and those keep their delegates, rows and scroll position
and only get `dataChanged` for their flags. The rest are removed and
appended. Each column has an `EntriesModel` of its own, so a column keeps
its model when its role changes (the peek that is entered becomes current,
the current one becomes an ancestor); the current column's shares the
listing and selected row of `FileBrowser.entries`. A removed column's model
is deleted on the next pass of the event loop, as its delegate may still
hold it.

**What is read.** Directories are listed on the GUI thread through the
`Browser`, as before. In the column layout the current directory is listed
through a `ListingCache` (64 directories, least recently used out), and
every listing a column shows is kept there, so entering the peek or going up
reads nothing again: on a walk down a chain nine folders deep and a jump
back up, each directory was read once. The cache is emptied whenever the
`Browser` is reopened (a reload, another subvolume) and when the layout
switches to list. No directory is read while one of the view's own jobs
(hash, copy out) reads through the `Browser`; the columns catch up when it
ends. In the list layout the columns model stays empty and nothing more is
read than before.

**Mouse.**

| Where | Click | Double-click |
|---|---|---|
| the current column | selects the row | the click, then a folder is entered |
| an ancestor | goes up to it, which selects the folder that was left again, then selects the row | the same |
| the peek | enters its folder, then selects the row | the same |
| a column's empty space | in an ancestor goes up to it, in the peek enters it; nothing in the current column | |

A step on the way to the clicked row does not show the folder passed: the
previous preview stays up, dimmed, until the clicked row's arrives. A
double-click enters a folder with nothing selected in it, as in the list. A
file or folder dragged out of any column is staged as in the list
(`stageColumnForDrag`, the same 1 GiB limit); neither the selection nor the
current directory changes.

**Widths.** The panes are a `SplitView`, the pattern of the sidebar in
`Main.qml`: the directory tree (list layout only), the strip or the table
(it takes the rest), the preview card. The gaps between them are the
handles; a short bar shows on hover and while dragging.
`Settings.browseTreeWidth` (200) and `browsePreviewWidth` (280) are written
when a handle is let go, and only for the pane the drag changed. When the
window is too narrow for them and a 320 px middle, the preview gives way
first, then the tree; when even the least widths (tree 150, middle 240,
preview 220) do not fit, as with a wide sidebar in a small window, they
shrink together, so no pane runs past the window's edge.

All columns share one width, `Settings.browseColumnWidth` (240), shown no
wider than the strip less one pixel, so a width chosen in a wider window
still shows one whole column. Each column ends in a 1 px divider with a 9 px
grip over it. Dragging any divider sets the width of all of them (Finder
does that with Option held); the dragged divider stays under the pointer
while the strip scrolls by what the columns to its left gained, and the
width is written on release. A double-click on a divider fits the longest
name in any column, scrolled into view or not (icon, margins and chevron
included), but not wider than half the strip, so the current column and the
peek still stand side by side, unless the columns are wider already.

**Where the strip scrolls.** Whole columns from the left edge, with the
current column and the place of the peek after it at the right end; when the
two do not fit side by side, the current column whole at the left. The
peek's place counts whether a folder or a file is selected, so moving
between them does not shift the strip. It is set when the current column
changes, when the strip's width changes and after a fit; otherwise (a
selection step, the peek coming or going) only when the current column or
the peek's place is out of view. A scroll by hand or a divider drag is left
alone until then.

**The list layout, names first.** The Name column takes the room; Size is 72
px, Modified 124, Mode 84. Below a 600 px list the Mode column goes, below
460 the Modified one too, and the header follows. Names elide in the middle.
In both layouts a name that does not fit shows whole in a tooltip after 600
ms. The breadcrumb box above the panes now shrinks with the row, keeps the
end of the path in view and fades its start, so a deep path no longer pushes
the buttons and the preview card out of the window.

Not covered:

- **Real input.** Keys went through `browseView.keyPressed()` and synthetic
  key events; clicks, double-clicks and the divider and handle drags through
  synthetic mouse events. Real key presses and a real pointer are left to a
  check by hand. Drag-out from a column was not run through the QML (it
  starts a native drag that blocks a scripted run); `stageColumnForDrag`
  was.
- **Settings written and the relaunch.** The writes of the layout and the
  widths (a run without `DRSTEIN_BROWSE_LAYOUT`), a stored value other than
  "columns" or "list", and the hand-over to the elevated copy are written
  and read, not run.
- **Left as they are, for the owner to decide.** While a file is selected
  the peek's place stays blank at the right; after a fit in a wide window
  this can hide the root column behind an empty half strip. Narrow columns
  still elide next to blank strip space in a wide window: filling the strip
  would change the widths on every step. After a divider drag a partly shown
  column can sit at the left edge until the next step.
- **Large folders.** A fit reads every row of every column through
  `EntriesModel.get()`, which formats all roles, dates included: fine for
  hundreds of entries, not measured for tens of thousands. The cache counts
  directories, not entries; on a path more than about 63 folders deep a step
  back may read a folder again and reset its column.
- **A small window with a wide sidebar.** At 960 px with a 420 px sidebar
  the list layout's preview card is 173 px wide; it stays inside the window
  but clips its own "auto" box and "Copy out…" button.
- None of this has been built or run on Linux or Windows.
