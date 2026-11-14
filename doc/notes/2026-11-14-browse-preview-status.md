# Browse: previews, arrow keys, preview toggle — status on 2026-11-14

Working notes for whoever continues this change. The design itself is in
`doc/design/01-application-architecture.md` §2.6 (core) and §10 (app side);
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
exercised, and the review was one reviewer's pass plus a read of the new
SVG code, not the multi-reviewer pass that was planned.

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

1. **Real key presses** in an active window, by hand: Up/Down through
   `photos/`, hold Down in `many/`, Enter into a folder, Backspace out,
   after clicking the toggle, after a dialog closes. Check the selection
   cue too: accent tint and bar while the list has the keyboard, grey
   while it has not.
2. **An independent multi-reviewer pass** over the change, as first
   planned (the review workflow, run `wf_b44fcf78-356`, was stopped before
   it returned anything; the lenses were concurrency and lifetime, core
   logic and tests, decoding and painting, QML keys and focus,
   integration and portability).
3. **Linux / Windows build** of this change (the Mac file is guarded in
   CMake; nothing was compiled elsewhere; a reading for GCC 13 and MSVC
   found nothing).
4. **The user's SD card image**
   `/Users/andrii/Projects/dr_stein/mmcblk0_luks-ext4-test_media.img`: MBR,
   one partition type 0xE8, LUKS2 aes-xts-plain64, argon2id t=8 m=1 GiB p=4.
   The project's fixture passphrase does not open it. It needs the
   passphrase from Andrii; nothing else was tried and nothing should be.

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
- At the 960 px minimum window width the entries list is narrow (tree 220,
  card 280 keep their widths). Not part of this change.

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
build/debug/src/core/drstein_core_tests            # 73 cases, 3487 assertions
```

Scripted run (the app quits by itself only when `DRSTEIN_SCREENSHOT` is set;
always set `DRSTEIN_PREVIEW` so the stored settings are not written):

```
DRSTEIN_PREVIEW=1 DRSTEIN_VIEW=browse DRSTEIN_DELAY=7000 \
DRSTEIN_SCREENSHOT=/abs/shot.png \
DRSTEIN_SCRIPT='Qt.createQmlObject("import QtQuick; Timer { interval: 1200; running: true; onTriggered: { browseView.browser.enter(\"photos\"); browseView.browser.select(16) } }", window)' \
"build/debug/src/app/Dr Stein.app/Contents/MacOS/Dr Stein" /abs/media-exfat.dmg
```

Script entry points: `browseView.browser.*` (select, enter, up,
moveSelection, previewOnce, previewKind, previewNote, previewLoading) and
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
