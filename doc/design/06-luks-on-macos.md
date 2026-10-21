# LUKS on macOS

Goal: plug in a LUKS-encrypted disk (or open an image of one), type the
passphrase, read the files on a Mac. Written 2026-10-21 after a survey of what
libstein and the app already do; this is the plan for closing the gap.

## What already works (verified on this Mac, 2026-10-21)

libstein opens LUKS1 and LUKS2 in process: `container::Luks` parses both
headers (LUKS2 JSON included), unlocks a slot with PBKDF2 or Argon2i/id, does the
anti-forensic merge and the digest check, and exposes the payload as a
decrypting `BlockDevice` (aes-xts-plain64, 512 or 4096-byte sectors, iv_tweak).
The probe tries every passphrase in `probe::Options::passphrases` on every LUKS
node and hangs a `Decrypted` child under it, probed like a device, so
LUKS → LVM → ext4 / xfs / btrfs and LUKS → ext4 both come out as trees.

- CLI: `stein luks info|unlock|extract`, and `stein ls|cat|mount|attach ... --passphrase`.
  `stein mount <image> <dir>` with `STEIN_PASSPHRASE` mounted the ext2 inside the
  LUKS2 fixture at a user directory through the NFS loopback, no root.
- App: a locked node shows a lock and an "Unlock…" button; the passphrase goes
  through `Workspace::unlock`, which re-probes with it (in a job, off the UI
  thread); the decrypted payload appears as a row with Browse and Mount enabled;
  Mount serves it read-only over NFS and the Mounts dialog lists it. Driven by
  script end to end on the LUKS2 fixture.

So "implement LUKS" is not a port: it is hardening, performance, and the UX of
a real disk. The survey (nine agents over both repos, design docs and the
cryptsetup defaults) found the gaps below.

## Gaps, by what the user hits first

### A. Unlock cost and the app's handling of it
- Argon2 runs all lanes on one thread. cryptsetup benchmarks LUKS2 slots to
  about 2 s with 4 lanes and up to 1 GiB; our single-threaded reference pays
  roughly 4× that, per slot, per attempt.
- Every re-probe (hot-plug, a repair, a Hex write, Refresh) re-runs the KDF for
  every stored passphrase. Nothing caches the unlocked master key.
- A wrong passphrase gives no error: the "Unlock…" button just stays, and the
  wrong string is kept and retried for the rest of the session.
- After a successful unlock the selection jumps to the disk row; nothing says it
  worked, and the payload row is one line below.
- Any typed passphrase switches on VeraCrypt trial decryption for every
  unrecognised region ≥ 256 KiB on every probe (9 PBKDF2 candidates each). On an
  unidentified blob that is minutes of CPU per probe.
- The passphrase list is per source and shared between encrypted `.stein` images
  and every LUKS node on the disk; the image open uses only the first entry.
- Argon2 memory from the header is uncapped: a damaged slot can ask for 4 TiB.
- No cancellation inside the KDF; the job's Cancel waits for it.

### B. Header robustness
- LUKS2 secondary header and checksum are never consulted: a damaged primary
  JSON fails instead of falling back to the good copy at `hdr_size`.
- dm-integrity, online re-encryption and `config.requirements` are ignored, not
  refused: such a volume "unlocks" and reads garbage.
- `unlock()` stops at the first slot with an unsupported KDF/AF hash instead of
  trying the next one.
- Tokens (systemd-tpm2, fido2, clevis, recovery) and keyslot priority are not
  parsed: TPM slots look like passphrase slots and are tried at full cost.
- Only aes-xts-plain64 is accepted although serpent/twofish/camellia XTS and the
  ripemd160/whirlpool hashes exist in the crypto core (for VeraCrypt).
- No detached-header open (`--header`), no header backup/restore as a piece.
- `std::stoull` on JSON strings can throw on a crafted header.

### C. The mount itself on macOS
- Works, but: the volume shows as "127.0.0.1:/" in Finder, the mountpoint is a
  temp directory Finder does not show, the only hint is a toast, and the Mount
  tooltip says FUSE.
- An external `umount` or a Finder eject is not noticed: the serving loop and the
  Mounts entry stay.
- Thread safety: the Browse reader (UI thread) and the mount reader (NFS
  threads) share the same decrypting device chain. The leaf FileDevice has a
  mutex; `ImageDevice`/`MappedDevice` chunk caches do not.
- Every read re-decrypts; XTS runs one AES block per virtual call; no cache
  above the payload. Fine for the fixture, slow for a photo library.
- No end-to-end test: the only mount test is plain ext4, the only LUKS test
  stops at probing an ext2 payload.

### D. Real disks on this Mac
- A physical disk needs Full Disk Access plus the elevated relaunch; the
  selected disk is not carried through the relaunch (images are), and the
  passphrase is lost with it. The CLI needs sudo for `/dev/rdiskN`.
- No fixtures for what real disks look like: cryptsetup defaults (argon2id,
  1 GiB, p=4), 4096-byte sectors with data beyond the first block, 256-bit keys,
  two slots plus a token, LVM inside LUKS, a partitioned disk with a LUKS
  partition, detached header. All need cryptsetup on the Linux laptop.

### E. Out of scope for now
- Writable mounts (would need filesystem writers and a write-capable backend).
- Key slot add/remove/change and luksFormat (the primitives exist; the JSON
  rewrite with checksums on both copies is the work).
- An "Attach" action (hdiutil over the decrypted payload) for filesystems macOS
  mounts natively; the CLI has `stein attach`, the app does not.

## Plan

1. **Library hardening (B)**: refuse integrity/reencrypt/requirements; secondary
   header with checksum and seqid; continue past unsupported slots; tokens and
   priority into `LuksSlotInfo`; keyBytes from the segment; guarded number
   parsing; Argon2 memory cap; serpent/twofish/camellia XTS and the two hashes;
   `Luks::open(header, data)` plus `--header`.
2. **Unlock performance (A)**: Argon2 lanes on threads; a cancel hook checked
   between slices, wired to `Progress`; an unlocked-key cache keyed by LUKS UUID
   in `probe::Options` so re-probes reuse the master key; XTS multi-block AES
   path and a bounded read cache above the payload.
3. **App behaviour (A, C)**: wrong passphrase → toast and forget it; select the
   payload row after unlock; dialog names the node; VeraCrypt trial only on an
   explicit "Try VeraCrypt" action; hide Unlock when unsupported and say why;
   mountpoint named after the label, "Reveal in Finder", per-platform tooltip;
   notice external unmounts; a mutex in the image device caches; zero keys and
   passphrases on close and quit.
4. **Real disks (D)**: carry the selected disk through the elevated relaunch and
   prompt once after; fixtures from the laptop; a LUKS stick made with
   cryptsetup for the hands-on test.
5. **Tests**: LUKS2 → filesystem → NFS mount in `test_mount`; the app's unlock
   chain in `test_topology`; the new fixtures under `tests/fixtures/luks`.

Each step lands separately with its own tests; the order is what a user with
an encrypted Ubuntu disk or a GNOME-Disks USB stick would hit first.
