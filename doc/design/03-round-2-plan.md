# Round 2: feedback of 2026-10-07 evening

Worked through in this order; each item gets a status line and notes as it
lands, so the work survives a session cut. Status: `[ ]` todo, `[~]` in
progress, `[x]` done, `[r]` researched only (by request), `[?]` question back.

## A. Visibility of image facts
- [x] A1 Stored (compressed) size of an image is not shown; only the virtual size.
      → sidebar: "8.1 GB" stays the size column; the subtitle gains "177 kB on disk";
        identity line gains "stored 177 kB".
- [x] A2 Creation time only lives in the file name.
      → identity line and Topology device card show `created 2026-10-07 23:28`
        (manifest `created`, shown in local time).

## B. Restore target UX
- [x] B1 The open source must be selectable as a target (it is the obvious target);
      the target list no longer excludes it (only the image being restored is excluded).
- [x] B2 Choosing an image file from disk in the Restore form adds it to the sidebar and
      makes it the open source (theme colours), as "Open image file…" does.
- [x] B3 The chosen target is marked in the sidebar in red (danger shade, every scheme),
      the same red as the Restore button; a new target moves the red mark.
- [x] B4 Target rows: a mono, space-padded device column first (`/dev/disk4`, `/dev/sda`,
      `file` for images), then the name. Both in the list and in the closed combo.

## C. Safety during writes
- [x] C1 While a job runs, tabs grey out and everything but Cancel is blocked.
      Expert mode gains "Allow using the app during reads and writes" with a warning;
      hidden unless Expert mode is on.

## D. Restore flow
- [x] D1 Passphrase is asked (and checked against the image) before the typed
      device-name confirmation; no delays or counters.

## E. Partitions / Topology on real devices
- [x] E1 Partition rows in the Partitions view could not be selected on the stick
      (row → partition index was a heuristic on name + size); the index now travels
      with the row.
- [x] E2 "Unmount, Refresh: nothing" — the edit session did not reopen after a refresh
      or an unmount; it now reloads on refresh and shows why it cannot open, with Retry.
- [x] E3 Topology shows OS device ids per partition (`disk5s2`, `sda1`, `nvme0n1p1`)
      next to "Partition 2", and in the details card.
- [x] E4 Mount button: when the OS already has the partition mounted, show the
      mountpoint and offer Unmount instead of our own mount.

## F. Settings under root
- [x] F1 Palette and scheme could not be changed in the elevated copy. Cause found:
      the relaunch passes DRSTEIN_PALETTE / DRSTEIN_SCHEME in the environment and
      Settings treated the environment as an override that wins over the stored
      value, so the switch wrote the setting and the getter kept returning the
      environment. Environment values are now one-time defaults.

## G. Elevation
- [x] G1 The top-bar label is already a button when not elevated
      ("Not elevated · relaunch…"); when elevated it is an indicator. (Done in the
      previous pass; the screenshot in the feedback was taken in the root copy.)
- [x] G2 The prompt is native: macOS authorisation dialog (osascript "with
      administrator privileges"), polkit agent via pkexec on Linux, UAC on Windows.

## H. Research (by request, no fix unless trivial)
- [r] H1 Restore of a 178 kB image took as long as a full 8 GB write. Why, and what
      the options are (libstein side). → full note: `04-restore-zero-chunks.md`.
- [x] H2 Hot-plug: can the app be told about insert / remove / mount / unmount on
      macOS, Linux, Windows instead of a Refresh button?
- [r] H3 disk vs rdisk on macOS; /dev/sda1 on Linux without mounting.

## K. Restore: write only the non-zero ranges
- [x] K1 Option in the Restore form, recorded in the confirmation and the report.
- [x] K2 Region-aware variant in libstein: "Zero free space only" zeroes what the
      partition table calls free and keeps every partition entry of any type; no
      table understood → nothing zeroed; byte counts planned from the chunk map
      before writing; straddling chunks split at the boundary. Tests on both sides;
      CLI `--zeros gaps`. See `04-restore-zero-chunks.md`.
- [x] K3 "Zero free space only" is the default when the target is a removable drive;
      byte-identical stays the default for fixed disks and files; an explicit choice
      sticks for that image.

## J. Someday (research)
- [ ] J1 Full Disk Access has to be granted again after every rebuild, and System
      Settings' "Quit & Reopen" did nothing. Why: TCC stores the grant against the
      app's code-signing identity. A debug build is ad-hoc signed by the linker, and
      an ad-hoc signature's designated requirement is the binary's own hash, which
      changes with every build, so macOS sees a different app each time. The fix is
      the standard one: sign the bundle with a stable certificate (a self-made
      "Code Signing" certificate from Keychain Access is enough for development;
      Developer ID for releases) so the requirement becomes "bundle id + certificate"
      and survives rebuilds; `codesign --sign "Dr Stein Dev" --force --deep` in a
      post-build step. "Quit & Reopen" goes through Launch Services: it sends the
      Apple quit event to the running app and relaunches the bundle. A copy started
      from a terminal with sudo is a root process outside the user's Launch Services
      session, so neither half can reach it; a normally launched (double-clicked)
      copy should respond. Not a blocker; revisit when packaging.

## I. Tables
- [x] I1 Browse: size / modified / mode columns drift with their text; make them
      fixed-width columns (also Topology and Partitions rows, same cause).

## Notes

### Status 2026-10-08

Everything above is in the tree except the libstein-side restore speed-up (H1,
left as research per your note). Verified on this Mac: A, B, C, D, F, I by
screenshot on image files; E1 and E3/E4 by core tests (the stick needs root to
open, which this session cannot do by itself); H2 by attaching a disk image with
`hdiutil` while the app ran: it appeared in the sidebar on its own.

### H2 Hot-plug (implemented)

`src/app/platform/device_watcher*`: one debounced `devicesChanged` signal that
makes the workspace re-list sources and re-read the open disk.
- macOS: DiskArbitration session on the main run loop (appeared, disappeared,
  description changed; the last covers mount and unmount). Verified.
- Linux: inotify on `/dev` (device nodes) plus `poll()` on `/proc/self/mounts`
  through a QSocketNotifier (the kernel signals POLLPRI when the mount table
  changes). No libudev dependency. Written, not run.
- Windows: `WM_DEVICECHANGE` (`DBT_DEVICEARRIVAL`, `DBT_DEVICEREMOVECOMPLETE`,
  `DBT_DEVNODES_CHANGED`) through a native event filter. Written, not run.
The Refresh button stays as the manual re-read of the open disk (its tooltip
says so when hot-plug is active). This is OS code and should move into
libstein's platform layer in a libstein session.

### H1 Why the 178 kB image took a full-disk write (researched 2026-10-08)

`image::restoreImage` copies through `copyDevice` with
`skipZeroChunksOnWrite = !options.writeZeroChunks`, and `RestoreOptions::writeZeroChunks`
defaults to `true` ("false only when the target is known to be zeroed"). So every
all-zero chunk (1 918 of the stick's 1 920 chunks) was written as 4 MiB of zeros:
8 GB at the stick's write speed, which is the minutes you saw. Nothing was
decompressed or verified slowly; it is the zero writes.

This is the correct default for a target of unknown content: a restore must leave
the target byte-identical to the image, and skipping zero chunks would leave
whatever the stick held before in those ranges. Three ways out exist, in order of
safety:

1. `discardZeroChunks` (already in `RestoreOptions`): ask the device to discard
   (TRIM / UNMAP) zero ranges instead of writing them. Fast when the device honours
   it and reads zeros afterwards (most SSDs, NVMe; many USB sticks do not). libstein's
   `BlockDevice::discard` falls back to writing zeros when the device refuses, so it
   is never less correct. It is `false` by default and the platform devices need
   the per-OS call (`BLKDISCARD` / `fcntl(F_PUNCHHOLE)` on files, `DKIOCUNMAP` on
   macOS, `IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES` on Windows). Worth checking
   which of those the platform devices implement; that is a libstein session.
2. Read-before-write: read each target chunk and skip the write when it is already
   zero. On a device whose reads are much faster than its writes (SSDs) this wins;
   on this stick (22 MB/s read) it would not help.
3. `writeZeroChunks = false`: skip zero chunks outright. Only right when the target
   is known blank (freshly discarded or zero-filled). The UI could offer it as an
   explicit expert option with that exact warning.

Recommendation: implement the discard calls in libstein's platform devices and turn
`discardZeroChunks` on by default in the app's restore (it degrades to today's
behaviour); keep `writeZeroChunks = false` as an expert opt-in. Not done in this
round, per your note.

### H3 disk vs rdisk, and Linux partition nodes

macOS exposes every disk twice: `/dev/diskN` is the buffered block device (goes
through the unified buffer cache, any offset and length), `/dev/rdiskN` is the raw
character device (direct to the driver, no cache, reads and writes must be
sector-aligned, several times faster for sequential imaging). libstein opens
`rdisk` and aligns through `AlignedDevice`; that is why the identity strip says
`/dev/rdisk4` while Disk Utility says `disk4`. Both name the same device, and
partitions follow the same pattern (`disk4s2` / `rdisk4s2`).

On Linux the kernel scans the partition table when a disk appears, so `/dev/sda1`,
`/dev/nvme0n1p1`, `/dev/mmcblk0p1` exist whether or not anything is mounted; the
`p` separator appears when the disk name ends in a digit. Windows has no per-partition
device file; volumes are `\\.\C:` or `\\?\Volume{GUID}` and are reached through
the disk's offsets, which is what libstein does.
