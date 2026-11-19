# Browse: previews, arrow keys, preview toggle, column view — status on 2026-11-14

Working notes for whoever continues this change. The design itself is in
`doc/design/01-application-architecture.md` §2.6 (core), §10 and, for the
column view, §11 (app side);
this file is the state of the work: what exists, what was proven, what is
left, and what was decided along the way.

## 1. The request

> Can you please ensure that when partition is browsed, and a filetype is
> supported for preview -- it is previewed. Also please implement moving
> through files with arrows. And it would preview new selected file. Also
> there probably should be a toggle, that selects whether files are previewed.
>
> I also added an image of small SD card, made on linux with various images.
> Its name is `mmcblk0_luks-ext4-test_media.img`.

## 2. Where it stands

The three features are implemented and work in scripted runs of the real app.
The fix round and a review of the riskiest code were done later the same day
(§7, "Done on 2026-11-14"), and the whole change was then committed and
pushed in both repositories (libstein first, then dr_stein with the
submodule pointer). Real key presses in an active window were never
exercised. The review was first one reviewer's pass plus a read of the new
SVG code; the planned multi-reviewer pass ran in the evening and its 28
findings are in §13. Its one open finding, S4 (the cost of drawing an SVG),
was then limited rather than fixed, and the rest of the SVG work postponed
(§14).

Later the same day the owner asked for names that fit and for a column view
as in Finder, as the default. That was built on top of this work and is
summarised in §12; its design is in `01-application-architecture.md` §11.

## 3. Why the old preview failed

- `FileBrowser::select()` called `core::preview()` on the UI thread, which
  read at most 256 KiB. Any picture larger than that was "truncated" and
  shown as a hex dump. That is the "supported but not previewed" case.
- Only png/jpeg/gif/bmp were sniffed, bmp by two bytes ("BM").
- The picture reached QML as a base64 `data:` URL string.
- The entries list had no keyboard handling and never had focus.
- There was no way to stop the app reading a file on selection.

## 4. Facts established (fact-finding pass, all read from the code)

- **Readers are not thread-safe, devices are.** `stein::fs::Reader`
  implementations keep unlocked lazy caches (FAT, exFAT, APFS, btrfs, XFS,
  HFS+, ISO, UDF, SquashFS, F2FS, EROFS). Every block device tolerates
  `readAt()` from several threads (file device, `.stein` image device, vdisk,
  XtsDevice, CachedDevice, OS disks). Readers own their devices through
  `shared_ptr`, so a reader outlives the probe tree. `FileBrowser::mount()`
  already opens a private reader for its own thread.
  => the preview worker owns a **second reader**; it never touches
  `FileBrowser::m_browser`.
- A second reader is cheap for ext (1 KiB superblock) and NTFS; FAT/exFAT
  each load their own FAT copy lazily (up to 256 MiB on huge volumes).
- libstein had unsynchronised lazily initialised static tables in
  `inflate.cpp` and `bzip2.cpp`: a real data race once two threads
  decompress (ThreadSanitizer: 10 reports on the old code, 0 on the new).
- **Qt 6.10.2 on this Mac decodes only** bmp cur gif ico jpeg pbm pgm png ppm
  svg svgz xbm xpm. The Qt Image Formats add-on is not installed, so Qt alone
  has no webp, tiff, icns, heif, jp2, tga.
- `QImageReader` has a process-global 256 MB allocation limit. Only jpeg and
  svg honour `setScaledSize` natively; a 9000×9000 PNG is refused even with a
  scaled size. `read()` cannot be cancelled. It is reentrant and works on a
  plain worker thread; format detection is by content.
- QtSvg draws a local file named by `<image xlink:href>`: an SVG from an
  untrusted disk must be vetted before it is rendered.
- There is no test harness for `src/qt`; only `src/core` has doctest tests.
  `stein::mount::makeSingleFileReader` over a `MemoryDevice` serves arbitrary
  bytes as a one-file reader and is what the new core tests use.
- Views are eager children of a StackLayout; nothing gave a view keyboard
  focus; window shortcuts use only Open, Refresh, Ctrl+, and Ctrl+1..7.

## 5. What was built

### Core (Qt-free, tested) — `src/core`
- `PreviewLimits { head 256 KiB, image 64 MiB, svg 1 MiB, chunk 1 MiB }`.
- `preview(reader, inode, stat, limits, cancelled)`: sniffs the head; a
  recognised picture up to `limits.image` is read whole in cancellable chunks;
  above it the result is Hex + truncated with the format in the note; SVG is
  an Image only when `svgSafeToRender()` passes, else Text with a note.
  Read failures are a value of kind `Unreadable`; the only error returned is
  cancellation.
- `sniffImageFormat`: png jpeg gif bmp webp tiff ico cur icns heic avif jp2
  j2k psd pnm xpm, each with a plausibility check (bmp no longer matches any
  file starting "BM"). TGA has no magic and stays hex.
- `svgSafeToRender`: refuses `<!ENTITY`, elements whose local name is
  `script`, `image` or `foreignObject` (whatever their prefix), event
  attributes, `@import`, `xml:base`, and any `href` / `url(` not starting
  with `#`.
- Text shows at most `PreviewLimits::text` (64 KiB); `Preview::textNote`
  says "first 64 KiB of …" when the text is not the whole file.
- `openReader()` (what `Browser::open` uses), `Browser::up()` / `jumpTo()`
  return the directory left, `movedIndex()`.
- `PreviewLoader` (`preview_loader.hpp/.cpp`): one detached worker that owns
  the second reader and serves only the latest request. One mutex; `Deliver`
  runs under it, only for the latest ticket, never after the destructor
  returned; the destructor never joins; `Transform` runs on the worker with
  no lock held.
- Two bugs fixed in passing: the UTF-8 tail-trim loop was quadratic on
  non-UTF-8 text (63.7 s for 256 KiB of Latin-1, and it dropped half the
  text); `jumpTo(SIZE_MAX)` emptied the breadcrumbs.
- Tests: new `src/core/tests/test_preview.cpp` (19 cases, extended in the
  fix round), no sleeps.

### libstein
- `src/core/src/inflate.cpp`: fixed-Huffman tables are a function-local
  static. `src/core/src/bzip2.cpp`: the CRC table is a compile-time constant.

### Qt layer — `src/qt`
- `Settings.previewFiles` (default true, key `ui/previewFiles`).
  `DRSTEIN_PREVIEW=0|1` overrides it and, while set, the setter never writes
  QSettings. The elevated relaunch passes the variable on.
- `decodePreview()` (`preview_decode.cpp`): QImageReader first (jpeg and svg
  straight to the fitted size; others only when width×height×4 < 256 MiB),
  then ImageIO on macOS (`preview_decode_mac.cpp`, C API, thumbnail at the
  box size with the EXIF transform), then a plain-words problem. Result fits
  a 2048 px box. A vetted SVG is first counted with `QXmlStreamReader`
  (`svgCostRefusal`): more than 100 000 elements once `<use>` and
  `url(#id)` references are followed, a reference loop, or nesting plus
  references deeper than 128 levels, and it is shown as text with that
  reason. Additions by the implementer: sources above 16384×16384 are
  refused; small SVGs are drawn at 512 px; ICO/CUR show the largest, deepest
  entry (hand-written directory parsing: review it).
- `PreviewSurface` (QQuickPaintedItem): centred, aspect-fit, never above one
  image pixel per screen pixel; a checkerboard (two theme colours) behind a
  picture with an alpha channel.
- `FileBrowser`: lazy loader with epoch + ticket, 80 ms keyboard debounce,
  150 ms grace before "loading", cache (12 entries, 96 MiB), stalled-worker
  replacement (10 s, at most 3 per browser), `previewOnce()`,
  `moveSelection / selectFirst / selectLast / activateSelected`, `up()` and
  `jumpTo()` re-select the directory left. Holes closed: a reload wanted
  during this object's own job is deferred; the hash result is matched by
  inode + name. No request starts while the UI is locked by a job.
  `previewKind`: none, off, loading, empty, text, hex, image, unreadable.

### QML — `src/app/qml`
- `BrowseView.qml`: `keyPressed(key, modifiers, autoRepeat)` on the root is
  the whole key handler (Up/Down, PageUp/PageDown, Home/End, Enter/Right into
  a folder, Backspace/Left up; the last four ignore auto-repeat). The list
  takes focus on a row click, a click on empty list space, a switch to Browse
  and when a job ends, never from an open dialog. Preview card: picture,
  text, hex, "reading…", dimmed previous content, the off explanation with
  "Preview this file". The toggle is an `StCheck` labelled "auto" in the card
  header. The Hex button reads "Image" while hex is shown over a picture.
- `SettingsDialog.qml`: "Preview files in Browse" with a sublabel.

### Docs
- README (Browse sentences, `DRSTEIN_PREVIEW`, script functions, test count
  73), `01-application-architecture.md` §2.6 rewritten and new §10,
  `02-ui-translation.md`.

### Files
Modified: `README.md`, `doc/design/01-application-architecture.md`,
`doc/design/02-ui-translation.md`, `src/app/CMakeLists.txt`,
`src/app/main.cpp`, `src/app/qml/dialogs/SettingsDialog.qml`,
`src/app/qml/views/BrowseView.qml`, `src/core/CMakeLists.txt`,
`src/core/include/drstein/core/browse.hpp`, `src/core/src/browse.cpp`,
`src/core/tests/test_browse.cpp`, `src/qt/file_browser.{hpp,cpp}`,
`src/qt/settings.{hpp,cpp}`, `src/qt/workspace.cpp`,
`libstein/src/core/src/{inflate,bzip2}.cpp`.
New: `src/core/include/drstein/core/preview_loader.hpp`,
`src/core/src/preview_loader.cpp`, `src/core/tests/test_preview.cpp`,
`src/qt/preview_decode.{hpp,cpp}`, `src/qt/preview_decode_mac.cpp`,
`src/qt/preview_decode_platform.hpp`, `src/qt/preview_surface.{hpp,cpp}`,
this note.

## 6. What was verified

- `cmake --build --preset debug`: clean, no warnings. qmllint: 0 warnings.
- Core tests 73/73 (54 before). Preview tests repeated 60 times. Mutation
  checks: removing the ticket check, the cancel flag or the chunk cancel
  check each fails tests.
- ThreadSanitizer (tree `build/wt-tsan`): whole core suite clean; an
  end-to-end run on the exFAT test image (browser re-listing on one thread,
  loader reading all 131 files on another) clean, and every picture
  byte-identical to a plain read.
- libstein suites run after the table change: test_core, test_fs,
  test_container, test_image, test_volume, test_probe all pass. Not run:
  test_app, test_block, test_layout, test_mount, test_ops, test_platform,
  test_pt.
- Decoder check (scratch program, real sources, worker thread): jpg, png,
  bmp, gif, ico, svg through Qt; tiff, heic, webp, icns, jp2, avif, tga and
  the 12000×9000 PNG through ImageIO; broken files give a problem, no crash.
  `leaks --atExit`: 0 leaks. Orientations 1–8 upright for JPEG, TIFF, HEIC.
- Scripted runs of the real app on the exFAT test image: every format above
  shows; 20 keyboard moves in a burst give one request and the picture of
  the final row; previews off reads nothing (no loader, no second reader)
  until "Preview this file"; reload / up / goTo while a big read is
  outstanding leave nothing stale; hash values equal `shasum -a 256`; the UI
  thread's worst stall while the huge PNG loaded was 8 ms.
- Keys: synthetic Qt key and mouse events (QtTest `TestEvent`, scratch only)
  reached the handler and behaved as designed. The window was never the
  active window.

- Fix round (later on 2026-11-14): build without warnings, qmllint 0
  warnings, core tests 73/73 (3487 assertions). Scratch programs in
  `<scratch>/opus/`: `svg_probe` (bypass candidates rendered with QtSvg,
  red pixels mean a file outside the SVG was read), `svg_bomb` and
  `svg_bomb_nocheck` (decodePreview with and without the count),
  `svg_chain` (QtSvg alone on a worker thread: 300 chained `<use>` fine,
  500 SIGBUS). Scripted runs of the app on `<scratch>/opus/media-opus.dmg`
  (exFAT: a long name, a transparent PNG, the nested-`<use>` SVG,
  aurora-wave.svg, an SVG with three `<use>`, a 600 KiB log), screenshots
  in `<scratch>/opus/shots/`, both schemes, list focused and not, previews
  on, off and switched off live. The stored settings were compared before
  and after: unchanged.

## 7. Not done, in order

The live list is now `doc/BACKLOG.md` (2026-11-19). Since then: the owner
checked the keys in both layouts (item 1: the pointer and drag-out are
left), and said the SD card's pictures are not private (item 4).

1. **Real key presses and a real pointer** in an active window, by hand,
   in both layouts; the owner will do this. In the list: Up/Down through
   `photos/`, hold Down in `many/`, Enter into a folder, Backspace out,
   after clicking the toggle, after a dialog closes. In columns (§12):
   Right and Left through a deep folder chain, Return selecting the
   folder's first entry, Left at the root, holding Down over folders (the
   peek follows only after the pause), clicks in an ancestor and in the
   peek, a divider dragged and double-clicked, the pane handles, a file
   dragged out of an ancestor column into Finder, the layout switched in
   Settings. Check the selection cue too: accent tint and bar while the
   list or the strip has the keyboard, grey while it has not, ancestors
   always grey.
2. ~~An independent multi-reviewer pass~~ over the preview change: done
   in the evening of 2026-11-14 (§13). What it left open is listed there.
3. **Linux / Windows build** of this change and of the column view (the
   Mac file is guarded in CMake; nothing was compiled elsewhere; a reading
   for GCC 13 and MSVC found nothing).
4. **The user's SD card image**
   `/Users/andrii/Projects/dr_stein/mmcblk0_luks-ext4-test_media.img`: MBR,
   one partition type 0xE8, LUKS2 aes-xts-plain64, argon2id t=8 m=1 GiB p=4.
   The owner gave the passphrase on 2026-11-14; it is not written down
   here. The column view was then run on an already decrypted copy of its
   ext4 payload (10 entries, Cyrillic names), with previews off, selecting
   only folders and non-picture files. Still to check, by the owner: the
   pictures on the card with previews on, which is what the request in §1
   was about.
5. **Tests for the SVG count** (`svgCostRefusal` in
   `src/qt/preview_decode.cpp`): deferred by the owner on 2026-11-14, to be
   written later. `src/qt` has no test harness (§4), so they need one, or
   the count moved into the core.

Open, noticed in the review and left alone:

- An SVG within the limits can still be slow by plain drawing (20 000 thin
  paths in 1 MiB: about 4 s in a release build); markers are counted once,
  not per vertex; rules in a `<style>` element are not followed by the count.
- After "Preview this file" with previews off, the loader and its second
  reader stay until the browser is reopened or the switch goes on and off.
- The tree's current-directory tint is as pale in the light scheme as the
  entries' selection was; not part of this round.

### Done on 2026-11-14 (fix round and review)

- *Names cut at half width*: the empty link-target `Text` is invisible, and
  a name is capped at 360 px only beside a link target. At the default
  window width the name column is still narrow (§8).
- *Selection cue*: accent tint (a stronger step in the light scheme) and a
  3 px accent bar while the list has the keyboard; grey tint and grey bar
  while it has not.
- *Bottom row text*: "lazy: a directory is read when it opens, a file when
  it is selected" / "… a file only when you ask", bound to `previewEnabled`.
- *Transparent pictures*: checkerboard in `PreviewSurface`.
- *Hostile SVG*: the count in `decodePreview` (§5). Ten levels of ten
  `<use>` took 7.6 s at depth 7 and were unbounded at depth 9; now 0.1 ms
  and shown as text. A chain of 500 `<use>` crashed QtSvg on a worker
  thread (stack overflow, SIGBUS) before; it is refused now.
- *Large text*: 64 KiB with a correct note (`PreviewLimits::text`).
- Review defect: `<s:image href="#x/red.png">` with `s` bound to the SVG
  namespace passed `svgSafeToRender`, and QtSvg drew the file `#x/red.png`
  from its working directory. Elements are now matched by local name;
  tests added.
- Reviewed and found sound: `PreviewLoader` ordering (delivery under the
  lock, nothing after the destructor, no lost wake-up, the per-request
  cancel flag, exceptions on the worker); `FileBrowser` epoch + ticket, the
  posted call (removed with the object, never posted after
  `~PreviewLoader`), the timers, the deferred reload, the cache key (entry
  offsets on FAT/exFAT are unique; the cache is emptied whenever the reader
  changes); the bindings on `selectionChanged`; the ImageIO object
  lifetimes and the box guarantee; the sniffers' bounds on short heads.

## 8. Accepted limitations (decided, not defects)

- The detached worker can still be running at process exit.
- A decode in flight cannot be cancelled; a small file selected right after
  the 12000×9000 PNG waits ~1.2 s behind it.
- Formats without a decoder on a platform show as hex with a note. Linux and
  Windows need Qt's Image Formats add-on for webp/tiff and have no HEIC.
- PNG eXIf orientation is ignored. A truncated JPEG shows as a partial
  picture with no note.
- Audio and video are not previewed.
- The second reader is opened on the UI thread, as `Browser::open` is.
- `svgSafeToRender` is conservative: Illustrator SVGs with `<!ENTITY>` and
  Inkscape SVGs with an embedded `<image>` are shown as text. So are SVGs
  that expand to more than 100 000 elements or nest more than 128 levels.
- At the 960 px minimum window width the entries list was narrow (tree 220,
  card 280 kept their widths). Since §12 the panes are resizable and give
  way in a narrow window, and the default column layout has no tree.

## 9. Conclusions

- The right split was: second reader + latest-only worker in the Qt-free
  core (testable, TSan-clean), decode and UI state in the Qt layer, keys in
  one QML function that scripts can call.
- macOS ImageIO as a fallback gives HEIC/WebP/TIFF/AVIF and very large
  pictures without a new dependency; it is the reason the 12000×9000 PNG
  shows at all.
- The toggle earns its place beyond taste: with it off no loader and no
  second reader exist, which is what one wants on failing media or an
  untrusted disk.
- The riskiest code was SVG rendering: both real defects of the review were
  there (a gate bypass by namespace prefix, and cost and recursion depth).
  The "hand-written ICO parser" is only the sniffer, bounds-safe through
  `Head::at`; ICO decoding itself is Qt's.

## 10. How to build, test, run

```
cd /Users/andrii/git/dr_stein
cmake --build --preset debug                       # app + core tests
cmake --build --preset debug --target all_qmllint  # must print no warnings
build/debug/src/core/drstein_core_tests            # 95 cases, 4344 assertions (89 before §14, 84 before §13, 73 before §12)
```

Scripted run (the app quits by itself only when `DRSTEIN_SCREENSHOT` is set;
always set `DRSTEIN_PREVIEW` and `DRSTEIN_BROWSE_LAYOUT` so the stored
settings are not written):

```
DRSTEIN_PREVIEW=1 DRSTEIN_BROWSE_LAYOUT=list DRSTEIN_VIEW=browse DRSTEIN_DELAY=7000 \
DRSTEIN_SCREENSHOT=/abs/shot.png \
DRSTEIN_SCRIPT='Qt.createQmlObject("import QtQuick; Timer { interval: 1200; running: true; onTriggered: { browseView.browser.enter(\"photos\"); browseView.browser.select(16) } }", window)' \
"build/debug/src/app/Dr Stein.app/Contents/MacOS/Dr Stein" /abs/media-exfat.dmg
```

Script entry points: `browseView.browser.*` (select, enter, up,
moveSelection, previewOnce, previewKind, previewNote, previewLoading; in
columns also columnClicked, columnActivated, enterSelectedAndSelectFirst,
columns.count, columns.get(i)) and
`browseView.keyPressed(Qt.Key_Down, 0, false)`, `browseView.moveSelection(n)`,
`browseView.pageRows()`.

## 11. Test media

Synthetic test patterns, not in the repository. On 2026-11-14 they were in
the session scratch directory
`/private/tmp/claude-501/-Users-andrii-git-dr-stein/ca4d207e-50e9-4a9c-ae93-0ddde3231ffc/scratchpad/`
as `media/` and `media-exfat.dmg` (exFAT, 96 MiB; note that `cp -R` on macOS
also wrote `._name` AppleDouble files). `/private/tmp` does not survive a
reboot; to rebuild (needs ffmpeg, cwebp, sips, hdiutil):

```
M=media; mkdir -p $M/photos $M/broken $M/many $M/notes; cd $M
ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
ff -f lavfi -i "testsrc2=size=4000x3000" -vf "noise=alls=30:allf=t" -frames:v 1 -q:v 2 photos/01-big-4000x3000.jpg
ff -f lavfi -i "mandelbrot=size=3000x2000:start_scale=1.5" -frames:v 1 photos/02-mandelbrot-3000x2000.png
ff -f lavfi -i "smptebars=size=1280x720" -frames:v 1 photos/03-smpte-1280x720.bmp
ff -f lavfi -i "rgbtestsrc=size=1600x1200" -frames:v 1 -compression_algo lzw photos/04-rgb-1600x1200.tiff
ff -f lavfi -i "testsrc=size=320x240:rate=5" -t 2 photos/05-animated.gif
ff -f lavfi -i "testsrc2=size=64x64" -frames:v 1 photos/06-tiny-64x64.png
ff -f lavfi -i "mandelbrot=size=2000x1500:start_scale=0.8" -frames:v 1 ../src-a.png
ff -f lavfi -i "testsrc2=size=12000x9000" -frames:v 1 photos/16-huge-12000x9000.png
sips -s format heic ../src-a.png --out photos/07-photo.heic; cwebp -quiet -q 85 ../src-a.png -o photos/08-photo.webp
sips -z 256 256 ../src-a.png --out ../src-icon.png
sips -s format ico ../src-icon.png --out photos/09-icon.ico; sips -s format icns ../src-icon.png --out photos/10-icon.icns
cp /Users/andrii/git/dr_stein/themes/examples/aurora-wave.svg photos/11-drawing.svg
sips -s format jp2 ../src-a.png --out photos/12-photo.jp2; sips -s format tga ../src-a.png --out photos/13-photo.tga
sips -s format avif ../src-a.png --out photos/14-photo.avif
ff -f lavfi -i "testsrc2=size=3000x4000" -vf "noise=alls=20:allf=t" -frames:v 1 -q:v 3 photos/15-portrait-3000x4000.jpg
ff -f lavfi -i "mandelbrot=size=160x120:rate=4" -t 10 -q:v 4 "many/img-%03d.jpg"
# plus README.txt, notes/long-log.txt (>256 KiB of text), notes/unicode.txt,
# broken/random.bin, broken/fake.bmp ("BM" + random), broken/truncated.jpg
# (first third of 01-big), broken/zero.png (0 bytes)
cd ..; hdiutil create -quiet -size 96m -fs ExFAT -volname MEDIA -layout NONE media-exfat.dmg
DEV=$(hdiutil attach -nobrowse -mountpoint ./mnt media-exfat.dmg | awk 'NR==1{print $1}')
cp -R media/ mnt/; sync; hdiutil detach -quiet $DEV
```

The column view (§12) used a second image, `tree-exfat.dmg` (exFAT "TREE",
64 MiB), with its source folder `tree-src/`, both in the scratch directory
of the session that built the column view,
`/private/tmp/claude-501/-Users-andrii-git-dr-stein/988cf335-a166-49b0-87b1-bf83c0a64b48/scratchpad/`:
`Projects/2026/…/v7/` (a chain nine folders deep with a `notes-N.txt` in
each and a long-named file at the bottom), `Names/` (very long, Ukrainian,
Japanese and emoji names, one without an extension, a hidden file, a
90-character `.tar.gz`), `Many/` (300 `.log` files and two subfolders),
`Mixed/` (four folders each with `child/leaf.txt`, two text files, two
PNGs) and an empty folder. The `hdiutil` lines above rebuild it from
`tree-src/` with `-size 64m -volname TREE`. `fix/bad-exfat.dmg` in the
same place is a copy with one folder's first cluster set out of range: its
column shows the read error.

## 12. Column view and layout (2026-11-14)

The owner's request, later the same day:

> Yes, loosen the layout so names fit.
>
> Also I wanted to ask you to create a column view as it is in MacOS
> finder, instead of 2 separate columns. And make it the default view.
> Option to change it can live in settings.

The design is in `01-application-architecture.md` §2.6 (core) and §11 (app
side); `02-ui-translation.md` has the Browse row.

### Built
- Core: `columns.hpp/.cpp`. `planColumns` (one column per breadcrumb, the
  last current, a peek when a folder is selected), `keptColumns` (which
  columns survive a step), `rowOfName`, `classifyClick` and `ListingCache`
  (64 directories, least recently used out, read errors not cached). New
  `test_columns.cpp`, 11 cases.
- Qt: `Settings.browseLayout` ("columns", the default, or "list") and the
  widths `browseTreeWidth`, `browsePreviewWidth`, `browseColumnWidth`;
  `DRSTEIN_BROWSE_LAYOUT=columns|list` decides the layout for one run, and
  such a run stores neither it nor the widths; the elevated relaunch
  passes it on. `ColumnsModel` (`FileBrowser.columns`) with an
  `EntriesModel` per column that stays with its directory;
  `columnClicked`, `columnActivated`, `stageColumnForDrag`,
  `enterSelectedAndSelectFirst`; the peek's own 80 ms timer.
  `EntriesModel` now shares its listing instead of copying it, and has
  `get(row)`. In the list layout the columns model stays empty.
- QML: `BrowseView.qml` is a `SplitView` of three resizable panes; new
  `BrowseColumns.qml` (the strip) and `BrowseList.qml` (the table, names
  first: Mode and Modified give way in a narrow pane). `keyPressed()`
  dispatches by layout; a tooltip shows an elided name whole; the
  breadcrumb box shrinks and fades its start instead of pushing the
  buttons out of the window; the bottom row is laid out by hand and names
  the layout's keys. Settings has "Browse layout" (Columns | List).
- Reviewed by three reviewers (core and facade, QML and keys, the look):
  12 findings, 11 fixed. A click in an ancestor or the peek briefly showed
  the preview of the folder passed; a column that failed to list was never
  read again; gaps in the tests, shown by mutation (three cases added);
  the divider drag drifted from the pointer and jumped on release; the
  strip shifted between a file and a folder, could cut the current column
  and showed columns wider than itself; a fit could push the peek out of
  view; the list layout's panes could run past a narrow window; the key
  hint was cut. The twelfth is left to the owner (below).

### Verified
- `cmake --build --preset debug --clean-first`: two warnings, both from
  before this change and in files it does not touch (`structs.cpp`,
  `window_chrome_mac.mm`). qmllint: 0 warnings (the old "unused import"
  info in `FrameResizer.qml` only). Core tests 84/84, 3741 assertions; the
  columns cases 11/11, 254 assertions. Five mutations of `columns.cpp` (no
  move to front in `find`, folding before the exact match, no replacement
  in `put`, titles compared in `keptColumns`, the peek decided by the
  directory entry's type) each fail the tests.
- Scripted runs on macOS, previews on and off, both schemes, at 960×620,
  1280×800 and 1600×1000, on TREE and MEDIA (§11):
  - The nine-level chain walked by clicks and keys: the columns to the left
    kept their models at every step, and each directory was read once.
  - The peek: unchanged 40 ms after a key step and there by 150 ms; at once
    after a click.
  - Clicks in an ancestor, in the peek and on a column's empty space; a
    double-click in the peek; a click during a job ignored; a change during
    a job applied when it ended.
  - Right, Return, Enter, Left, Backspace, Right on a file, Left at the
    root, auto-repeat and Ctrl+Down in both layouts, through `keyPressed()`
    and synthetic key events.
  - Divider drag (the divider under the pointer at every 5 px step; clamped
    at 140 and 640 and at the strip's start), fit (240 → 364 px at 1280,
    524 at 1600), the pane handles, the layout switched both ways.
  - List layout: `columns.count` stayed 0 and nothing was read for columns
    (15 reads: 1 tree load, 14 directories).
  - A folder that cannot be read shows the error in its column; a click
    reads it again, a key step does not.
  - `stageColumnForDrag` from an ancestor: the copy's SHA-256 equals the
    copy-out's; path and selection unchanged.
  - Previews: kind and note the same in both layouts.
- The owner's SD card (§7.4), its decrypted payload, in columns with
  previews off: the root column listed it, an empty folder showed as the
  peek, a click entered it, Left went back up, Left at the root did
  nothing.
- The stored settings: byte-identical to a snapshot taken before the work,
  with no `browse*` keys.

### Left open
- Real key presses and a real pointer (§7.1); drag-out from a column was
  run through `stageColumnForDrag` only, never as a real drag.
- Written, not run: storing the layout and the widths (a run without
  `DRSTEIN_BROWSE_LAYOUT`), reading a stored layout other than "columns"
  or "list", the relaunch hand-over.
- For the owner to decide: narrow columns still elide beside blank strip
  space in a wide window (the twelfth finding; filling the strip would
  change the widths on every step). While a file is selected the peek's
  place stays blank, and after a fit in a wide window the root column can
  hide behind an empty half strip. After a divider drag a partly shown
  column can sit at the left edge until the next step.
- Not measured: a fit over tens of thousands of entries. Limits: the cache
  counts directories, not entries; `rowOfName` folds ASCII letters only;
  breadcrumb and listing inodes were compared on ext4 and exFAT only.
- At 960 px with a 420 px sidebar the list layout's preview card clips its
  own "auto" box and "Copy out…" button.
- From before this change, not touched: the top bar does not fit a 960 px
  window on any view. (The breadcrumb's doubled slash after the root,
  "TREE exFAT / / Projects", also older, is fixed: the root crumb is the
  "/" itself and separators go only between names.)
- Not built on Linux or Windows.

### Files
Modified: `README.md`, `doc/design/01-application-architecture.md`,
`doc/design/02-ui-translation.md`, this note, `src/app/CMakeLists.txt`,
`src/app/main.cpp`, `src/app/qml/dialogs/SettingsDialog.qml`,
`src/app/qml/views/BrowseView.qml`, `src/core/CMakeLists.txt`,
`src/qt/file_browser.{hpp,cpp}`, `src/qt/settings.{hpp,cpp}`,
`src/qt/workspace.cpp`.
New: `src/core/include/drstein/core/columns.hpp`, `src/core/src/columns.cpp`,
`src/core/tests/test_columns.cpp`, `src/app/qml/views/BrowseColumns.qml`,
`src/app/qml/views/BrowseList.qml`.

## 13. The multi-reviewer pass (evening of 2026-11-14)

Four reviewers, each with a fresh context, the code of `d85a92e` (with
`a7272f7` where it touches the same code, and libstein `81ebdf4`) and the
claimed invariants as hypotheses to refute; none was given this note or the
design document. Lenses: (1) concurrency and lifetime, (2) SVG and parsing
security, (3) decode resource limits, (4) the preview's UI state in both
layouts. Lenses 1 and 2 ran on Fable, 3 and 4 on Opus. Each finding was then
checked by the coordinating session: by running the reviewer's repro again,
by a failing core test before the fix, by a scripted run of the app, or, where
none was possible, by reading the code path.

### Findings and verdicts

| Id | Finding | Verdict, how | Fix |
|---|---|---|---|
| C1 | btrfs numbers subvolumes per reader: the worker's reader could not resolve any file inside a subvolume ("unreadable") | confirmed, code (`fs_btrfs_reader.cpp` `treeIndexFor`); core test with a reader that renumbers | requests carry the path; the worker looks it up and checks type, size, mtime |
| C2 | quitting during an SVG render crashed (static destructors under the detached worker) | confirmed in the app: 2 of 4 runs exit 139 | `main()` destroys the QML objects, waits ≤ 1 s for workers, else `_Exit`; 6 of 6 runs exit 0 |
| C3 | the 10 s stall rule opened a reader on the GUI thread behind a read that hangs (devices serialise reads): the UI froze | confirmed, the reviewer's repro on `AlignedDevice` (3 s freeze) | replace only when `activity()` says the worker is in the transform of a superseded ticket |
| C4 | the "worker may lag" flag stayed set: a slow legitimate read was restarted | confirmed, code | flag removed; `activity()` decides |
| C5 | "at most 3 abandoned workers" reset on every reopen | confirmed, code | bound on workers alive in the process (`workersAlive()` < 3) |
| C6 | a read in flight when a job locks the UI went on, and its result was shown | confirmed, code | the lock cancels it and holds; resent on unlock |
| U1 | Copy out / Copy all acted on a row index after a re-probe during the folder dialog (older code) | confirmed in the app (re-probe, then accept) | `prepareCopy(row)` fixes the entry when the dialog opens; a re-read disk copies nothing and says so |
| U2 | text and hex opened at the previous file's scroll offset | confirmed in the app | the view returns to the start when its text changes |
| U3 | after "Preview this file" with previews off, the loader and reader stayed | confirmed, code (was listed in §7 as open) | dropped when the selection settles to "off" |
| U4 | the Hex/Image button is sticky and was enabled and labelled "Text" where it does nothing | half: stickiness is by design and kept; the label and the enabled state were wrong | enabled only for a text or a picture with a hex head; label from what is shown |
| U5 | turning previews on showed the "off" panel while the file was read | confirmed in the app | "loading" at once |
| U6 | columns: the peek is listed by inode but entered by name (two folders of one name) | confirmed, core test | `Browser::enter(name, inode)` everywhere a listed folder is entered |
| U7 | a column read again after a failure lost its highlighted row | confirmed, code | the earlier row is kept only for the very same listing |
| D1 | a progressive JPEG of 171 bytes made libjpeg hold 2 GiB (coefficient buffer), whatever the scaled size | confirmed, repro (2.08 GiB) | core refuses multi-scan JPEGs above 256 MiB of coefficients |
| D2 | a JPEG of 10 000 scans kept the worker busy for minutes (linear in scans) | confirmed, repro | core refuses more than 100 scans |
| D3 | ImageIO decodes an interlaced PNG (1 GiB) or a float TIFF (8 GiB) whole | confirmed, repro (killed at 3 GiB) | 512 MiB whole-decode budget for anything but baseline JPEG, plain PNG and HEIF |
| D4 | SVG filters and style-sheet masks escape the count | confirmed; same as S3/S4 | see S3, S4 |
| D5 | the stall rule multiplied bombs and then wedged previews | confirmed; same cause as C3/C5 | see C3, C5 |
| D6 | the Qt path peaked at twice its budget (scaling before converting); an ICO or GIF header named a false size | confirmed, repro (GIF of 8000² labelled 1 × 1) | convert in place, then scale; size from the decoded picture |
| D7 | valid UTF-8 above 256 KiB with one control character showed as hex | confirmed, repro and core test | the head is judged with a cut character trimmed |
| D8 | a text starting "icns" was taken for an icon file and read whole | confirmed, repro and core test | ICNS needs a plausible length and first entry |
| S1 | ids defined twice: the count followed decoys, a 600-link `<use>` chain crashed the worker (SIGBUS) | confirmed, repro (exit 138) | a reference counts every definition |
| S2 | `url (#id)` with a space: followed by QtSvg, seen by neither gate nor count; crashes and 4.8 GiB | confirmed, repro (exit 138) | gate and count read `url\s*(` |
| S3 | references in `<style>` rules were not counted: crash, hang; selector matching cost unbounded | confirmed, repro (exit 138) | style rules followed to the elements their selectors may match; rules × elements ≤ 10 M |
| S4 | element count is not render cost: one path of 90 000 segments took 251 s | confirmed; **limited, not fixed** | layers weighted, 1024 px; then (§14) 512 KiB, a 2 s limit after which the SVG is text, two drawings at once; a given-up drawing still runs to its end |
| S5 | 125 nested masked groups took 4 GiB within the limits | confirmed, repro | masks and filters add 2000 each; refused now |
| S6 | `<?xml-stylesheet href="#name.css"?>` read a host file named "#name.css" | confirmed, repro (canary) | the gate refuses `<?xml-stylesheet` |
| S7 | the gate reads bytes, QtSvg the declared encoding (UTF-7 on a Qt with ICU) | plausible, not reproduced (this Qt has no ICU) | the gate refuses encodings other than UTF-8, ASCII, Latin-1 |

Claims that held under attack, per the reviewers: the loader's locking,
delivery and wake-ups (also under ThreadSanitizer); the epoch and ticket
filtering; the cache keys; no shared mutable state between two readers
(ThreadSanitizer over 111 fixture filesystems); libstein's table fix; the
sniffers' bounds; the 16384² cap before ImageIO; the 2048 px box; the env
overrides never storing settings; `.svgz` never reaching QtSvg.

### Verified
- `cmake --build --preset debug` clean; qmllint 0 warnings (the old Info in
  `FrameResizer.qml`); core tests 89/89, 4135 assertions (5 new cases, and
  new checks in three existing ones).
- The reviewers' crafted files through the real `core::preview()` and
  `decodePreview()` (scratch harness): every D and S repro above is now
  refused in milliseconds or bounded; a plain 16384² PNG and a 16384² HEIC
  still decode (140 and 90 MiB); `aurora-wave.svg`, an Illustrator-style
  SVG with style-sheet clip paths, a mask, a marker and a filter, and the
  reviewers' benign style-sheet files still render.
- Scripted runs of the app (exFAT image of a heavy SVG, 12 000 gradient
  rects, two long texts, a small SVG): quitting during a render exits 0 in
  6 of 6; U1, U2, U5 as in the table; the column layout's peek click, Left
  and Right. The stored settings were compared with a snapshot afterwards.
  Note: a run that opens an image from the command line also writes
  `images.lastDir`; it was put back by hand. The recipe in §10 does not
  cover that.

### Left open
- **S4, the cost of drawing an SVG.** No static count bounds it: a single
  self-crossing path, long dash patterns, thousands of gradient fills.
  Limited in §14 (512 KiB, 2 s, two at once); the real fix, a separate
  process the app can kill (which would also contain a crash), is designed
  and postponed in `doc/design/08-svg-out-of-process.md`.
- **TIFF and other formats ImageIO was not measured on** are refused above
  512 MiB whole, though some (a 16-bit stripped TIFF) would have streamed.
- C1 was checked with a renumbering test reader; no btrfs image with
  subvolumes was browsed. C3 and C6 were not driven in the app.
- Not built on Linux or Windows (§7.3), where S7 matters most.

## 14. SVG: a size limit and a time limit, the rest postponed (late 2026-11-14)

The owner set S4 aside as far from the app's priorities, asked what breaks
if it is left, and then chose two cheap limits: "1/2 MB and 2 seconds of
rendering. If it didn't render -- it failed." The out-of-process design
that had been drafted for S4 is kept, postponed, in
`doc/design/08-svg-out-of-process.md`, with its measurements and the
questions that were open.

### Built
- core: `PreviewLimits::svg` is 512 KiB (was 1 MiB); a byte more and the
  SVG is text, "SVG image above the 512 KiB preview limit".
- core: `deadline.hpp`, `DeadlineRunner`: a call on a thread of its own,
  waited for up to a deadline; a late one is abandoned and runs to its end
  holding a place; past the places a call waits within its deadline or is
  `Busy` (01 §2.6). Six cases in `test_deadline.cpp`.
- Qt: `decodePreview()` draws a vetted SVG through a process-wide runner
  with two places and a 2 s deadline, on a copy of the source. Timed out:
  text, "SVG image that did not draw within 2 s", remembered in the
  preview cache. Busy: text, "SVG image not drawn: earlier ones are still
  drawing", not remembered (`DecodedPreview::retry` →
  `PreviewContent::remember`).
- `main()`: after the workers, waits 250 ms for SVG drawings, then `_Exit`.

### Verified
- `cmake --build --preset debug` clean; qmllint 0 warnings (the old Info in
  `FrameResizer.qml`); core tests 95/95, 4344 assertions; the deadline cases
  50 runs in a row without a failure.
- Scratch harness, the real gate and `decodePreview()` in one process on a
  worker thread: `aurora-wave.svg` 6 ms as before; `e4_zigzag`,
  `e3_dash_big`, `e1_fill_12000` refused by size; `e2_blur_10` and
  self-crossing paths of 48.5 and 436 KiB given up at 2.0 s; after two
  given up, the next three (`aurora-wave` among them) "earlier ones are
  still drawing" after 2 s each. Without the limit those files take 2.6 s,
  10 s, 61 s, and over 90 s for a 394 KiB dashed path (release Qt).
- Scripted run of the app (exFAT image `svg-exfat.dmg` with seven files,
  list layout, `DRSTEIN_PREVIEW=1`): aurora an image at once; the 48.5 KiB
  path "loading", then text with the note at 2 s; the 436 KiB one the same;
  the blur file, with both places held, "earlier ones are still drawing";
  the 892 KB file refused by size; a plain SVG drawn again once the first
  path's drawing ended (about 10 s); back on the 48.5 KiB path, the text at
  once from the cache, no new drawing. GUI-thread heartbeat: worst gap
  94 ms. CPU sampled from outside: about 100 % per given-up drawing, 200 %
  with two, never more. Quit with a drawing running: exit 0 (both runs).
  A screenshot showed the card with the note. Settings were snapshot before
  and compared after; `images.lastDir` was written by the run and put back.

### What is left as it is
- A given-up drawing keeps a core busy until it ends: minutes for a crafted
  file of a few hundred KiB. Two of them stop all SVG drawing until one
  ends (other previews are not affected).
- A crash inside QtSvg still ends the app.
- Everything else on SVG is in 08 §5–§6: the separate process, tests of
  `svgCostRefusal` (deferred by the owner), S7, Linux and Windows.
