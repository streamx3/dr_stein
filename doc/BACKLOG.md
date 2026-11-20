# Backlog

The open work for Dr Stein and libstein in one list. It is kept current
as items land or are decided; how each item was found, and the evidence
behind it, stays in the notes and design documents named at the end.

Updated 2026-11-20.

## Waiting on the owner

- **Browse with a real pointer.** The keys were checked by the owner on
  2026-11-19 in both layouts and work. Still to try by hand:
  - clicks in an earlier column and in the column after the current one;
  - a divider dragged, and double-clicked to fit;
  - the pane handles in the list layout;
  - a file dragged out of a column into Finder;
  - the layout switched in Settings.
- **The column next to a selected file.** It stays blank so the strip
  does not shift between files and folders. Fill it (file details, or the
  preview as Finder does), or leave it blank.
- **Full Disk Access after every rebuild** (round-2 plan J1). An ad-hoc
  signature changes with every build, so macOS treats each build as a new
  app. Fix: the owner makes a self-signed "Code Signing" certificate in
  Keychain Access, then a post-build `codesign` step uses it.
- **The Linux package, installed for real** (needs sudo; the checklist is
  at the end of `05-linux-bringup.md`). A test package from 2026-11-19 is
  in `~/drstein-pkg/` on the laptop; one built from the committed tree
  gets a clean version.
  - `sudo apt install ./dr-stein_<version>_amd64.deb`;
  - start Dr Stein from the menu (its icon, the panel icon, Open With for
    a `.img`) and as `dr-stein` from a terminal;
  - Elevate from the installed app: the pkexec dialog, then the elevated
    copy on the same view and source;
  - a mount through fuse3;
  - `sudo apt remove dr-stein`, and check that nothing is left.
- **Two decisions from the Linux round** (2026-11-19):
  - libstein's `test_mount` fails on Linux: FUSE shows `lost+found`
    (0700) as 0555, the test wants 0500, the NFS rule macOS uses. Make
    FUSE strip the write bits only, or loosen the test on Linux.
  - The package's Qt brings its own image decoders, which get no
    distribution updates, and Browse decodes pictures as root after the
    elevated relaunch. Now: all decoders kept, previews on when elevated,
    rebuild on every Qt patch release (README). Alternatives: previews off
    when elevated, or fewer decoders.
- **Checks at the Linux laptop, with a person there:**
  - a USB stick plugged in with the app open;
  - raw writes to a stick (`BLKZEROOUT`, `BLKDISCARD`, table re-read,
    restore);
  - the window chrome (drag, double-click to maximise, edge grips);
  - a file dragged into Nemo.

  Also open: whether the test user gets a `NOPASSWD` sudo rule limited to
  the test binaries.

## Next up

- **Windows build of the November work**: previews, the column view, the
  SVG time limit (`core::DeadlineRunner`). Nothing of it has been compiled
  on Windows; Linux built and ran it on 2026-11-19.
- **The 960 px window.** The top bar does not fit on any view. In the list
  layout with a 420 px sidebar, the preview card cuts off its "auto" box
  and "Copy out…" button.
- **Theme loading.** The `.qtheme` format is done (07); the loader is
  planned in 07 §12. Six roles (link, selectionBg, selectionText, tagFill,
  tagText, scrim) are still ramp steps hard-coded in views.

## App

- Hex edits and repairs inside containers and volumes (LUKS payloads, LVM
  LVs) cannot be written back yet.
- No hint in the app for `mbr.logical_lost`.
- The details card could say "header only" for a detached LUKS header
  opened alone.
- `ProfilesView.qml` lines 92 and 99 warn on every scripted run.
- SMART; the privileged helper (library side).
- The sidebar can list two images with the same file name from different
  folders; only the tooltip tells them apart.
- **Linux package** (made 2026-11-19, `05-linux-bringup.md`). Left:
  - before it goes to anyone else: ICU's full LICENSE text in
    `copyright` (it has only ICU's permission notice), a real Maintainer
    (`DEBFULLNAME`, `DEBEMAIL`), a build from a committed tree;
  - only Qt 6.12.0 has been packaged; with Qt 6.7 to 6.9 Qt's deploy
    cannot exclude plugin types, so a few unused plugins come along;
  - Wayland only through XWayland: no Wayland platform plugin is shipped;
  - the `Files:` paths in `copyright` name the configured prefix, even
    after `cmake --install --prefix` elsewhere;
  - `qt-third-party.txt.gz` takes every attribution page of each shipped
    Qt module, Android, macOS and Windows pieces included;
  - lintian has not been run (not installed on the laptop).
- **Linux, the November work**: built and run 2026-11-19 (Topology,
  Browse in both layouts, previews). Not run there yet: mounting, the
  Hex, Image, Profiles, Tools and Partitions views, the light scheme.
- Columns:
  - storing the layout and the widths was written but has not run yet;
    the first change in a normal launch exercises it;
  - `rowOfName` folds ASCII letters only;
  - the listing cache counts directories, not entries;
  - a fit over tens of thousands of entries is not measured;
  - after a divider drag, a partly shown column can sit at the left edge
    until the next step.

## Previews (low priority)

The owner, 2026-11-19: previews are no priority.

- **No video or PDF preview.** `.webm` and `.pdf` show as hex. The
  pipeline decodes still images only; video would need a frame grabber.
- **The pictures on the owner's SD card** may be viewed: the owner says
  they are not private. The decrypted copy lived in a scratch folder that
  has since been cleared, so a new look needs the passphrase again.
- TIFF and other formats ImageIO was not measured on are refused above
  512 MiB whole.
- After "Preview this file" with previews off, the loader and its second
  reader stay until the browser is reopened.
- The tree's current-directory tint is pale in the light scheme.
- SVG (postponed by the owner):
  - drawing in a separate process the app can kill (08); on Linux the
    helper must be found through `/proc/self/exe`, since started through
    `/usr/bin/dr-stein` the app's `applicationDirPath()` is `/usr/bin`;
  - tests for the SVG count;
  - the count does not follow `<style>` rules, and markers are counted
    once;
  - a drawing given up after 2 s keeps a core busy until it ends.

## libstein

- **LUKS:**
  - VeraCrypt has no key hook or cancel;
  - cipher key schedules are not wiped on destruction;
  - PBKDF2 is about 3.5 times slower than cryptsetup;
  - no physical LUKS stick has been tried by hand.
- **FAT times** are read as UTC (`dosTime` in `fs_fat_reader.cpp` uses
  `timegm`), but FAT stores local time without a zone: a file written at
  23:48 in Kyiv (UTC+2) shows as 01:48.
- **Not scheduled:**
  - MBR ↔ GPT conversion;
  - moving and resizing partitions with their data (M3);
  - FAT and exFAT level 2 (label and UUID writers, mkfs, fsck);
  - FAT long names and timestamps in the Hex structure tree;
  - `Uuid::randomV4()`.
- **MBR, CLI and test items**: "Deliberately left" in the round-2 plan.
  It has about fifteen of them, among them the fixtures to make on the
  Linux box, and the CLI has no automated tests.

## Decided

- **Column width** (owner, 2026-11-19): as in Finder. Columns start at a
  reasonable width and can be resized, but never widen on their own to fit
  long names. That is how they work now; a double-click on a divider fits
  the names only when asked.

## Where the details are

- `doc/notes/2026-11-14-browse-preview-status.md`: previews (§7, §13,
  §14) and the column view (§12).
- `doc/design/03-round-2-plan.md`: J1 and "Deliberately left".
- `doc/design/05-linux-bringup.md`: the Linux laptop, the November build
  there and the package; `README.md`: installing on Linux.
- `doc/design/06-luks-on-macos.md`: the LUKS review round.
- `doc/design/07-qtheme-format.md`: the theme format and its loader.
- `doc/design/08-svg-out-of-process.md`: the postponed SVG process.
