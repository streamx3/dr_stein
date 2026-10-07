# Why restoring a 178 kB image wrote 8 GB, and what to do about it

Research note for a libstein session (2026-10-08). Nothing here is implemented yet.

## What happened

The stick's image holds 1 920 chunks of 4 MiB; 1 918 are all-zero and stored as
nothing. `image::restoreImage` hands the image to `copyDevice` with
`skipZeroChunksOnWrite = !options.writeZeroChunks`, and `RestoreOptions::writeZeroChunks`
defaults to `true`. The copy loop therefore read each zero chunk from the reader
(cheap: the reader synthesises zeros), found it all-zero, and, because
`discardZeroChunks` is `false` by default, wrote 4 MiB of zeros to the stick. 1 918
chunks × 4 MiB = 8 GB at the stick's write speed. Nothing else in the path is slow.

The default is right for a target of unknown content: a restore promises that the
target reads back byte-identical to the image, and ranges that are zero in the
image must become zero on the target, whatever was there before.

## What the library already has

| Piece | State |
|---|---|
| `BlockDevice::discard(offset, length)` | virtual; base returns Unsupported |
| `BlockDevice::zero()` | tries `discard`, else writes zeros in 1 MiB pieces |
| Linux disk | `BLKDISCARD` ([platform_linux.cpp:153](../../libstein/src/platform/src/linux/platform_linux.cpp)) |
| macOS disk | `DKIOCUNMAP`, falls back to base ([platform_macos.cpp:148](../../libstein/src/platform/src/macos/platform_macos.cpp)) |
| Windows disk | no discard |
| `FileDevice` (raw `.img` targets) | no discard; a sparse file is created on `create()` only |
| `copyDevice` | `discardZeroChunks` → `target.discard()`, trusted when it succeeds ([copy.cpp:97](../../libstein/src/image/src/copy.cpp)) |
| `restoreImage` | passes both options through; both off by default |
| `SteinReader` | knows which chunks are stored (`storedChunks()`, chunk map); zero chunks are implicit |

## The catch: discard does not promise zeros

`BLKDISCARD` and `DKIOCUNMAP` are hints. A device may return the old data, zeros, or
anything for an unmapped range (the SCSI/ATA bits for "deterministic read zero after
trim" exist but are not exposed by these ioctls; Linux dropped
`discard_zeroes_data` in 4.12 for that reason). So `copyDevice`'s current
`discardZeroChunks` path, if switched on, could leave stale data on a device that
accepts the ioctl without zeroing. That is a correctness bug waiting to happen, not
a speed-up.

What *does* promise zeros:

| Target | Guaranteed-zero primitive | Speed |
|---|---|---|
| Linux block device | `BLKZEROOUT` (kernel issues WRITE ZEROES with deallocate where the device supports it: NVMe, SCSI, some UAS sticks; otherwise writes zeros itself) | fast when supported, never slower than our own writes |
| Linux file | `fallocate(FALLOC_FL_PUNCH_HOLE \| FALLOC_FL_KEEP_SIZE)` | instant |
| macOS file (APFS, HFS+) | `fcntl(fd, F_PUNCHHOLE, &fpunchhole_t)` | instant |
| macOS block device | nothing guaranteed; `DKIOCUNMAP` only | — |
| Windows file (sparse) | `FSCTL_SET_ZERO_DATA` after `FSCTL_SET_SPARSE` | instant |
| Windows block device | nothing guaranteed; `IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES` + `DeviceDsmAction_Trim` is a hint | — |

## Proposal for libstein

1. **Split the two meanings.** Keep `discard()` as the hint it is. Add
   `virtual Expected<void> zeroRange(offset, length)` with the contract "afterwards
   the range reads as zeros", implemented per device:
   - Linux disk: `BLKZEROOUT`; on failure fall back to writing zeros.
   - `FileDevice`: punch a hole (`fallocate` / `F_PUNCHHOLE` / `FSCTL_SET_ZERO_DATA`);
     fall back to writing zeros on filesystems without holes.
   - macOS and Windows disks: `DKIOCUNMAP` / `DeviceDsmAction_Trim`, then **read the
     range back**; if it is not all zero, write zeros. The read-back is what makes it
     correct; on a device where unmap works it is still a win because reading is
     cheaper than writing on flash, and on a device where unmap is refused the fallback
     writes immediately with no extra read.
   - Base: write zeros (what `zero()` does today).
   `BlockDevice::zero()` becomes a thin alias of `zeroRange()`.
2. **`copyDevice`: zero chunks go through `zeroRange()`**, always, instead of the
   `discardZeroChunks` flag. It is never less correct than writing zeros and often
   much faster. `discardZeroChunks` can go.
3. **Skip chunks that are already right (optional, adaptive).** Before writing a
   chunk, read the target's chunk and compare; skip the write when equal. This covers
   the case you hit (restoring the stick's own image back onto it: every chunk was
   already identical) and halves flash wear. The cost is one read per chunk, which
   hurts when the target differs everywhere, so make it adaptive: compare the first
   32 chunks; if fewer than, say, 70 % matched, stop comparing and write blindly for
   the rest. Expose as `CopyOptions::skipIdenticalChunks` (default on for restores).
4. **Let the reader say which chunks are zero.** `copyDevice` currently reads every
   chunk from the reader and runs `isAllZero` over 4 MiB. For `.stein` sources the
   chunk map already knows; an `isZeroChunk(i)` on `SteinReader`, consulted through
   the `ImageDevice`, saves the synthesis and the scan. Small, but it makes the zero
   path essentially free on the source side.
5. **Keep `writeZeroChunks = false` as the expert escape hatch** in the app
   ("the target is blank, skip zero ranges"), with the warning that it is only right
   after a full discard or zero-fill. Not needed once 1–3 land, but cheap to keep.

## Expected effect on the stick

The stick's raw write speed is the bound today. After the change:

- If the stick honours UNMAP (UAS sticks often do; bulk-only ones rarely), zero
  chunks cost one unmap plus one read-back each: roughly the read speed you saw
  (22 MB/s) instead of the write speed, so about 6 minutes instead of the full write.
- With "skip identical" and a stick that already holds the same data (your case),
  every chunk is read once and nothing is written: the read speed again.
- For a raw `.img` target on APFS/ext4/NTFS the zero chunks become holes: instant.

A stick that refuses UNMAP and holds junk still needs the 8 GB of zeros written;
nothing can avoid that short of not restoring the zeros, which is the expert flag.

## Side notes

- The app's restore should pass `verifyPayloadFirst` (it does, "Verify the image's
  checksums before writing anything") and could add "verify after restore" as a
  read-back compare; with 3 above the read already happens.
- `WinDisk` has neither `discard` nor `flush`-with-`FlushFileBuffers` semantics worth
  checking in the same session.

## Addendum (2026-10-08): skipping zeros on purpose, and SSDs

The first part of this note is about making a byte-identical restore fast. There is
a second, simpler stance, and for a disposable USB 2.0 stick it is the right one:
do not write the zeros at all. `RestoreOptions::writeZeroChunks = false` has been
in libstein from the start; the app now exposes it as "Write only the non-zero
ranges" in the Restore form, with the trade-offs spelled out and recorded in the
confirmation and the report.

### What skipping zeros does and does not do

- **Filesystems come back complete.** Every allocated block is in a non-zero chunk
  (or a stored chunk) and gets written. Free space is free space; the filesystem
  never reads it. This is exactly what partclone and Clonezilla do by default, so
  it is well-trodden ground.
- **Old bytes stay where the image is zero.** Inside a partition that means the
  previous occupant's deleted files remain recoverable with undelete tools (data
  remanence). That is the security cost, and it is the only one for the filesystem
  itself.
- **Gaps between partitions and the space after the last partition keep their old
  contents too.** If the previous layout had a partition where the new one has a
  gap, its superblock survives and probing tools (blkid, udev, LVM and mdraid
  auto-assembly on Linux, Dr Stein's own probe) may report a ghost filesystem or a
  stale PV/RAID member there. This is the practical risk, more likely to bite a
  hobbyist than the security one. The cheap mitigation is to still zero everything
  *outside* partitions (gaps, the end-of-disk area), which is a few MiB, and skip
  zeros only *inside* partitions. That needs a region-aware zero policy in
  `copyDevice` (the image's manifest already carries the topology), a small libstein
  change; it is the version I would make the default. Until then the option is
  all-or-nothing and the warning says so.
- **Verification after restore** must compare only the non-zero chunks; a byte-wise
  compare of the whole device would flag every skipped range. The restore report
  already distinguishes "written" from "read".

### SSDs: does writing zeros release anything? Does skipping them harm anything?

No and no.

- An SSD does not free a block because zeros were written to it. The controller
  stores the zeros (some controllers detect all-zero writes and dedupe them, which
  is why "writing zeros" can look fast on a modern NVMe, but that is an
  implementation detail, not a release). Blocks are released only by TRIM/UNMAP,
  which the *filesystem* issues for its free space after mount (`fstrim`, mount
  option `discard`, macOS does it periodically on APFS/HFS+ internal SSDs, Windows
  "Optimize Drives"). The zeros our restore writes into free space get trimmed
  away later anyway, which means they were pure write amplification.
- So skipping zero writes on an SSD is strictly better for the device: fewer
  program/erase cycles, less write amplification, no effect on block release
  either way. If you want the free space released right after a restore, the
  correct action is a TRIM of the free space (`fstrim` on Linux; on macOS it
  happens on its own for internal drives; external USB SSDs mostly never get
  trimmed through USB mass storage, only through UAS with UNMAP support), not a
  zero write.
- Cheap USB sticks have no TRIM at all and the weakest flash; writing 8 GB of zeros
  to one is the single worst thing to do to it short of the capacity test. Skipping
  zeros there is both faster and kinder.
- The one case where writing zeros is the right thing: you need the target to be
  byte-identical (forensic copy, a disk that will be imaged again and compared, a
  disk you are about to hand over), or the previous contents must not be
  recoverable. Those are the default, deliberately.

### Recommendation

Keep "write zeros" as the default (correct for strangers' disks), offer "write only
the non-zero ranges" as it is now, and in the libstein session make it region-aware:
zero the gaps and metadata areas always, skip zeros inside partitions when asked.
With that, the skip option can become the default for removable drives.

## Implemented (2026-10-08): region-aware zero policy in libstein

`image::ZeroPolicy { Write, Skip, SkipInside }` on `CopyOptions` and `RestoreOptions`
(`writeZeroChunks = false` and `skipZeroChunksOnWrite` remain as aliases of `Skip`).

- **Keep regions** (`image::keepRegionsOf(device)`): every entry of the partition
  table the image carries, whatever its type (unknown GUIDs, extended containers,
  APM map entries included), clipped to the device. No table, an unreadable table,
  or a table type the library does not parse: the whole device is kept. What cannot
  be read as a table is never treated as free space.
- **Split at the boundary**: a zero chunk that straddles a partition edge is written
  only outside the partition (`regionsOutside`), sector-exact; the test checks the
  last byte inside and the first byte outside.
- **Plan first** (`image::planZeroWrites(image, options)`): from the chunk map, with
  no payload read, how many bytes are zero chunks, how many will be written and how
  many kept, plus the keep regions and the reason ("GPT, 2 partitions"). The copy
  stats report the same two numbers afterwards (`zeroBytesWritten`,
  `zeroBytesSkipped`), and the test asserts plan == outcome.
- CLI: `stein image restore … --zeros write|gaps|skip` prints the plan before
  writing. App: "Zero ranges of the image" (Write them / Zero free space only /
  Skip them) in the Restore form, with the plan line under it and in the
  confirmation and report.

Defaults (app): a removable target starts on "Zero free space only"; fixed disks and
image files start on "Write them". The default follows the target until the user
picks a mode for that image; picking a new image resets it.
