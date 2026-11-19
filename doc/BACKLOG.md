# Backlog

The open work for Dr Stein and libstein in one list. It is kept current
as items land or are decided; how each item was found, and the evidence
behind it, stays in the notes and design documents named at the end.

Updated 2026-11-19.

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
- **Column width in wide windows.** The owner checked on 2026-11-19 that
  a column keeps its width when a longer name is selected or the window
  grows. Open: keep it so (a double-click on a divider fits the names), or
  let the columns widen on their own.
- **Full Disk Access after every rebuild** (round-2 plan J1). An ad-hoc
  signature changes with every build, so macOS treats each build as a new
  app. Fix: the owner makes a self-signed "Code Signing" certificate in
  Keychain Access, then a post-build `codesign` step uses it.
- **Checks at the Linux laptop, with a person there:**
  - a USB stick plugged in with the app open;
  - the `pkexec` dialog;
  - raw writes to a stick (`BLKZEROOUT`, `BLKDISCARD`, table re-read,
    restore);
  - the window chrome (drag, double-click to maximise, edge grips);
  - a file dragged into Nemo.

  Also open: whether the test user gets a `NOPASSWD` sudo rule limited to
  the test binaries.

## Next up

- **Linux packaging**: a `.deb` built by a script (CPack), or at least
  `make install` and `make uninstall`.
  - **Before building, ask the owner where the newest Qt is on that
    machine.**
  - Mint 22 / Ubuntu 24.04 ship Qt 6.4.2 and the app needs 6.5 or later,
    so the package has to carry its Qt.
- **Linux and Windows builds of the November work**: previews, the column
  view, the SVG time limit (`core::DeadlineRunner`). Nothing of it has been
  compiled off macOS.
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
  - drawing in a separate process the app can kill (08);
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
- **Not scheduled:**
  - MBR ↔ GPT conversion;
  - moving and resizing partitions with their data (M3);
  - FAT and exFAT level 2 (label and UUID writers, mkfs, fsck);
  - FAT long names and timestamps in the Hex structure tree;
  - `Uuid::randomV4()`.
- **MBR, CLI and test items**: "Deliberately left" in the round-2 plan.
  It has about fifteen of them, among them the fixtures to make on the
  Linux box, and the CLI has no automated tests.

## Where the details are

- `doc/notes/2026-11-14-browse-preview-status.md`: previews (§7, §13,
  §14) and the column view (§12).
- `doc/design/03-round-2-plan.md`: J1 and "Deliberately left".
- `doc/design/05-linux-bringup.md`: the Linux laptop.
- `doc/design/06-luks-on-macos.md`: the LUKS review round.
- `doc/design/07-qtheme-format.md`: the theme format and its loader.
- `doc/design/08-svg-out-of-process.md`: the postponed SVG process.
