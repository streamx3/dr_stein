# 08. SVG previews in a separate process (postponed)

Status: **postponed** by the owner on 2026-11-14 ("SVG is far from top
priority for this app"). What was done instead, and what this note keeps for
later, is below. Nothing of the design in §4 is built.

## 1. The problem

Finding S4 of the multi-reviewer pass (`doc/notes/2026-11-14-browse-preview-status.md`
§13): no static count bounds what drawing an SVG costs. QtSvg cannot be
interrupted, and it draws inside the app, so:

- a slow drawing keeps a core busy until it ends: one self-crossing path of
  90 000 segments with an even-odd fill took 251 s, a dashed path over 300 s;
- a crash in QtSvg ends the app. Three crafted files did that until the
  count was patched for them (S1–S3), and the next unknown bug would too;
- a code-execution bug in QtSvg would run with the app's rights, root after
  the elevated relaunch.

## 2. What was done instead (2026-11-14)

The owner chose limits that cost little:

- **512 KiB.** An SVG above it is shown as text without being drawn
  (`PreviewLimits::svg`, was 1 MiB). That turns away the reviewers' three
  slowest files (537 to 892 KB).
- **2 s, then it failed.** The drawing runs on a thread of its own through
  `core::DeadlineRunner` (01 §2.6). If it has not drawn within 2 s, the SVG
  is shown as text with "SVG image that did not draw within 2 s", and that
  is remembered with the preview, so the file is not drawn again on the way
  back.
- **Two at a time.** A given-up drawing is not stopped; it runs to its end
  unseen. At most two drawings run at once, given-up ones included, so SVG
  costs at most two cores. Past that, an SVG waits within its own 2 s for a
  place, or shows "earlier ones are still drawing", which is not remembered.
- **Quit** waits 250 ms for a given-up drawing, then ends the process with
  `_Exit` (as it already did for a worker stuck in a decode).
- `svgSafeToRender` (what an SVG may touch) and `svgCostRefusal` (the count
  of what it expands to) are unchanged.

What that leaves: the wait for a preview is bounded, the work is not. A
crafted file of a few dozen KiB can still keep a core busy for a while, and
two of them stop all SVG drawing until one ends. A QtSvg crash still takes
the app down.

Measured with the release Qt 6.10.2 on this Mac (arm64), drawing at 1024 px
with no limit:

| File | Size | Time |
|---|---|---|
| `aurora-wave.svg` | 631 B | 6 ms |
| ten full-picture blurs (`e2_blur_10`) | 851 B | 2.6 s |
| self-crossing path, 5 000 segments | 48.5 KiB | 10 s |
| self-crossing path, 45 000 segments | 436 KiB | 61 s |
| dashed path, 45 000 segments | 394 KiB | over 90 s |
| 8 000 gradient-filled rectangles | 383 KiB | 4.6 s |

In a scripted run of the app over those files (status note §14) the GUI
thread never stalled more than 94 ms, the app used about 100 % of a core per
given-up drawing and never more than 200 %, and quitting with a drawing
running exited 0.

## 3. When to come back to this

- When SVG previews matter more (a theme or icon workflow inside the app,
  user reports of SVGs that stay text).
- When the raster decoders are moved out of process for the same reasons
  (a hostile PNG, JPEG or TIFF in Qt's plugins or ImageIO); one helper and
  one runner would serve both.
- Before shipping builds to people who browse untrusted disks as root.

## 4. The design, as proposed

The drawing moves to a helper process that the app can kill.

```
preview worker (thread)                          drstein-svg-render (process, one per SVG)
  renderPreview → decodePreview(bytes,"svg",…)      sets its own limits, then QGuiApplication (offscreen)
    svgCostRefusal()        cheap pre-filter        reads at most the SVG limit from stdin
    core::runHelper()  ── SVG bytes on stdin ──▶     QImageReader("svg"), same size rule as now (512–1024 px)
      every 10 ms: cancel? deadline? memory?         writes a fixed header + raw premultiplied ARGB pixels
               ◀── header + pixels on stdout ──      exits 0
    core::parseSvgReply()   checks every size
    QImage
```

1. **The binary.** A separate small executable `drstein-svg-render`, linked
   to QtCore, QtGui and QtSvg only and placed next to the app binary (inside
   `Dr Stein.app/Contents/MacOS/` on macOS, in `/opt/dr-stein/bin/` on
   Linux). On Linux the app has to look for it through `/proc/self/exe`: started
   through the `/usr/bin/dr-stein` link, Qt's `applicationDirPath()` is
   `/usr/bin` (`05-linux-bringup.md`). None of the app's own start-up
   runs on hostile input. If it is missing, an SVG is text with a note. The
   alternative, a `--render-svg` mode of the app binary, needs nothing placed
   or deployed, and started as fast in the measurement below.
2. **One fresh process per SVG**, never reused. A warm start of an
   offscreen `QGuiApplication` with QtSvg took 3–20 ms here (1.2 s on the
   very first, cold one; linking QtQml, QtQuick and QuickControls2 as well
   cost almost nothing). Each file is isolated, and killing a drawing needs
   no protocol to restart a worker. A long-lived helper would save about
   60 ms on SVGs with text (the first glyphs), at the price of a framing
   protocol and state carried across files.
3. **Pipes in and out**: a socket pair, so a dead helper cannot raise
   SIGPIPE in the app (`SO_NOSIGPIPE` on macOS, `MSG_NOSIGNAL` on Linux). The
   SVG goes in on stdin. The reply on stdout is a fixed header (magic,
   version, status, width, height, source size, message length) and then
   width × height × 4 bytes, 4 MiB at most. The app treats the reply as
   untrusted: a wrong magic or status, a side above the one asked for, a
   byte count that does not match, or a message over 256 bytes is refused.
   stderr goes to `/dev/null`; QtSvg's warnings on a hostile file are
   unbounded. No shared memory, no PNG.
4. **Time.** 5 s to start and take all of the input, then the drawing limit
   (2 s, as now) to finish; the helper is killed past either. The note stays
   "SVG image that did not draw within 2 s: shown as text".
5. **Cancel, a newer request, quit.** `PreviewLoader::Transform` gets the
   worker's cancel flag (`const std::atomic<bool>&`), which a newer request,
   `cancel()` and `~PreviewLoader` already raise. The runner checks it every
   10 ms and kills the helper's process group, and it always reaps the
   helper before it returns: no helper outlives the call, and no core stays
   busy. The two-places bound and the `_Exit` on quit for SVGs go away.
6. **Memory.** The app polls the helper's physical footprint every 10 ms
   and kills it above 512 MiB: `proc_pid_rusage` → `ri_phys_footprint` on
   macOS (compressed pages included; `RLIMIT_AS` is not enforced there), RSS
   from `/proc/<pid>/statm` on Linux. Measured footprints after a drawing:
   5–32 MiB. The 1024 px bound on the picture stays.
7. **Backstops in the helper**, for when the app itself dies: `RLIMIT_CPU`
   of 10 s (the kernel ends it); `PR_SET_PDEATHSIG` on Linux, a thread that
   exits when `getppid()` changes on macOS; its own process group; every
   other descriptor closed at spawn (`POSIX_SPAWN_CLOEXEC_DEFAULT` on macOS,
   `posix_spawn_file_actions_addclosefrom_np` on glibc 2.34+), so the app's
   open disks never reach it; started as root, it drops to `nobody` before
   it reads the SVG.
8. **Fonts as now.** The app passes its default font family (`--font`), so
   text without `font-family` looks the same, and the offscreen platform's
   65 ms search for its missing "Sans Serif" is skipped.
9. **`svgCostRefusal` stays** as a pre-filter: it answers in under a
   millisecond with a precise reason and saves starting a process for files
   that would only be killed. The process becomes the guarantee.

Where the code would go:

- core, Qt-free and without libstein (so the helper can link it without
  libstein's dylibs): `helper_process.hpp/.cpp` (`runHelper(program, args,
  input, limits, cancelled)` → Finished, TimedOut, OverMemory,
  OutputTooLarge, Crashed with a signal, Cancelled, CouldNotStart; exit code,
  output, peak memory, elapsed), `svg_reply.hpp/.cpp` (the header, both
  ways), and the cancel flag on `PreviewLoader::Transform`;
- tests: the runner against a small Qt-free child built with the tests
  (echo, sleep, allocate, crash, flood stdout, ignore stdin, leave a
  grandchild), each checking that no process is left; the reply parser
  against truncated and lying replies; the loader passing its flag;
- `src/qt`: `decodePreview(…, cancelled)` calls the helper and maps each
  outcome to a note; `core::DeadlineRunner` is then no longer needed for SVG;
- `src/svg_render/main.cpp`, target `drstein_svg_render`, with install rules.

## 5. Questions that were open when it was postponed

Recommendations first.

1. A separate `drstein-svg-render`, or a `--render-svg` mode of the app.
2. A fresh process per SVG, or a long-lived helper.
3. The limits: 2 s to draw (5 s to start), 512 MiB, 1024 px.
4. Windows: show SVGs as text until a Windows build can run a job-object
   runner (memory capped by the kernel, the child killed with the job), or
   write that branch unbuilt, or keep drawing in-process there. Linux shares
   the POSIX code.

## 6. Other SVG work left

- Tests of `svgCostRefusal` itself: deferred by the owner on 2026-11-14.
- S7 (an encoding trick that needs a Qt built with ICU): plausible, not
  reproduced; the gate refuses encodings other than UTF-8, ASCII and Latin-1.
- A macOS sandbox profile or Linux seccomp for a helper.
- Linux and Windows: none of the SVG path has been built there.
