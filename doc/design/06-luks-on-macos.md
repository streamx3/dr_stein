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

## Status (2026-10-23)

One pass of eleven agents over both repos (libstein working tree on 6d0438a,
dr_stein on e6f1f49). Each step names what landed, with the API names and the
codes a caller sees; the numbers are under "Measured"; everything not done is
under "What remains" and "Fixtures to generate".

### 1. Library hardening (B) — done

`stein::container::Luks` (`src/container/include/stein/container/luks.hpp`):

- Both LUKS2 header copies are read (the secondary at the primary's `hdr_size`,
  or the nine cryptsetup metadata sizes 16 KiB..4 MiB scanned when the primary
  is unreadable); each copy's `csum` (`csum_alg` sha256 / sha1 / sha512 /
  ripemd160 / whirlpool) is verified over the binary header with the checksum
  zeroed plus the whole JSON area; the valid copy with the highest `seqid` wins,
  the primary on a tie; a copy whose checksum passes but whose JSON does not
  parse counts as damaged. `LuksInfo::headerCopyUsed`, `primaryValid`,
  `secondaryValid`, `headerSize`. Both copies damaged → `InvalidFormat` naming
  both; no header anywhere → `NotFound`.
- Refused with `LuksInfo::supported == false` and the reason in
  `unsupportedWhy` (`unlock()` and both `openPayload()` overloads return
  `Unsupported` with the same text): a segment `integrity` object, more than
  one segment, a segment type other than `crypt`, any
  `config.requirements.mandatory` entry, a `config.flags` entry outside
  {allow-discards, same-cpu-crypt, submit-from-crypt-cpus, no-journal,
  no-read-workqueue, no-write-workqueue}, a digest type other than pbkdf2 or an
  unknown digest hash, an unsupported segment cipher or mode. `LuksInfo::flags`
  and `requirements` expose the lists.
- Slots: every usable slot is tried, priority 2 first, then 1, then 0, ids
  ascending within a class. A slot `unlock()` cannot try (unknown KDF, a
  pbkdf2 or AF hash we lack, af type ≠ luks1, area type ≠ raw, an area cipher
  we lack, Argon2 memory above the machine cap or invalid parameters,
  `key_size` ≠ volume key, not bound to the segment's digest, a keyslot type
  other than `luks2` such as `reencrypt`) is skipped with
  `LuksSlotInfo::unsupportedWhy`; `Integrity` only after every usable slot
  failed; `Unsupported` ("no usable key slot (slot N: why; …)") when none is
  usable. `LuksSlotInfo::priority` and `tokens` (type strings:
  `luks2-keyring`, `systemd-tpm2`, `systemd-fido2`, `systemd-recovery`,
  `clevis`…). `LuksInfo::keyBytes` is the digest-bound slot's `key_size`,
  independent of JSON order. Every JSON number goes through `std::from_chars`
  (`parseU64` / `jsonU64` / `jsonU32`: no sign, no overflow, `InvalidFormat`
  otherwise); no `std::stoull` remains. A segment `size` given as a JSON
  number is the byte size (it used to fall through to "dynamic").
- Argon2 memory is capped at `crypto::argon2MemoryCapKiB()` = min(4 GiB,
  physical RAM / 2); a slot above it is skipped with the request named; the
  allocation is nothrow.
- Ciphers and hashes: `container::luksCipher(cipher, mode)` routes
  `<cipher>-xts-plain64` for aes, serpent, twofish, camellia (and kuznyechik)
  through `crypto::Xts`; cbc-essiv, cbc-*, xts-plain64be, xts-plain and
  `capi:` notation are refused by name. `container::luksHash()` maps sha256,
  sha1, sha512, ripemd160, whirlpool. Key-slot areas honour their own
  `area.encryption`.
- Detached header: `Luks::open(header, data)`. Header fields, key material
  and the digest come from `header`, the payload from `data`, and the stored
  offset (LUKS1 `payload_offset` in sectors, LUKS2 segment `offset` in bytes)
  is applied to the data device exactly as dm-crypt maps it: cryptsetup writes
  0 when formatting with `--header`, so a detached payload normally starts at
  byte 0 of `data`, while a `luksHeaderBackup` of an on-device header keeps its
  offset and still opens the same disk. `open(device)` is `open(device, device)`.
- CLI: `stein luks info` prints the header copy in use and whether the other
  copy is damaged, per slot the KDF, tokens, priority (preferred / ignored) and
  the "cannot try" reason, `flags:`, `requirements:`, and a final "cannot open
  in-process: …" line. `--header <file|device>` works for `luks
  info|unlock|extract` and for `ls|cat|cp|mount|serve|attach` (the data device
  is decrypted first by declaration, since it carries no LUKS signature). A
  LUKS layer met in the ls/cat/cp/mount/serve/attach chain without
  `--passphrase`, `--passphrase-file` or `$STEIN_PASSPHRASE` asks on a terminal
  with no echo ("LUKS passphrase for <device>: ", typed once and reused for
  lower layers); non-interactive runs keep the `Permission` error; `luks
  unlock|extract` still read one stdin line from a pipe and fail with
  `InvalidArgument` on an empty passphrase.

Tests: `test_container` 16 cases / 2627 assertions (Debug): header copies
(damaged primary, truncated primary with a recomputed checksum, size scan,
seqid both ways, every csum_alg), every refusal by rewriting both JSON copies
with the checksum recomputed, ten skip reasons on a preferred slot while slot 0
still opens, tokens and priority, eleven malformed numbers, cancellation, the
detached header (header backup + wiped disk, LUKS1 `payload_offset` 0 and
misplaced, data device too small, writes landing on the data device only),
cipher and hash names on patched LUKS1 headers, and the cryptsetup 2.8.4
fixtures of step 4 (`info()` against `luksDump`, payloads by master key,
unlocks by passphrase).

### 2. Unlock performance (A) — done in the library, wired into probe and app

- `crypto::argon2()` / `argon2id()` take a trailing `crypto::Argon2Options {
  std::function<bool()> cancelled; std::uint32_t threads = 0; }`: the lanes
  of a slice run on `std::thread` workers (min(parallelism,
  hardware_concurrency); p = 1 keeps the single-thread path); `cancelled` is
  polled before every slice → `Cancelled`. Surfaced as
  `container::UnlockOptions { cancelled }` on `Luks::unlock(passphrase,
  options)` and `Luks::openPayload(passphrase, readOnly, options)`; `unlock()`
  also polls between slots.
- `probe::Options` gained `masterKeys` (LUKS UUID → master key, tried before
  any passphrase through `Luks::openPayload(masterKey)`, no KDF; a foreign or
  stale key is skipped silently), `onUnlocked(uuid, masterKey)` (once per
  container a passphrase opened, on the probing thread, before the plaintext is
  probed; the receiver owns the bytes, the probe keeps no copy) and
  `cancelled` (polled between Argon2 slices, key slots and passphrases; once
  true the container is left as found with no note and `probe()` still returns
  the tree). The header is opened on every probe now, so a node says what it is
  without passphrases. Note codes, documented above `struct Note` in
  `topology.hpp`: `luks.header` (Warning: neither copy readable),
  `luks.unsupported` (Info: "cannot open in-process: " + `unsupportedWhy`; such
  a node is not unlockable and no key or passphrase is touched), `luks.unlock`
  (Warning: a passphrase failed for a reason other than being wrong),
  `luks.locked` (Info: passphrases given, none opens). Notes never carry key
  material or passphrases. `probeTcrypt` polls `cancelled` between passphrases
  only (`TcryptOptions` has no hook).
- `crypto::Xts` runs a single AES layer eight blocks per step with
  precomputed GF(2^128) tweaks on AES-NI / ARMv8 CE
  (`aesXtsEncryptHardware` / `aesXtsDecryptHardware` in `aes_hw.cpp`;
  `Xts::implementation()` reports "aes-ni", "arm-aes", "aes-portable" or
  "generic"; `Xts::setFastPath(false)` pins the generic path for tests);
  cascades and the other ciphers keep the per-block path. `Xts` is stateless
  and `out` may alias `in`. `container::detail::XtsDevice::readAt` decrypts at
  most 1 MiB per chunk through two per-device scratch buffers under a mutex
  taken per chunk (a 128 MiB request no longer allocates 256 MiB; the UI and
  NFS threads interleave).
- `block::CachedDevice` (`stein/block/cached_device.hpp`): a bounded LRU of
  fixed-size chunks (`Options { chunkSize = 256 KiB, budget = 64 MiB }`),
  thread-safe, write-through with invalidation, `invalidate()`, `stats()`.
  `Luks::openPayload(…, readOnly = true)` returns the `XtsDevice` wrapped in
  it; writable payloads stay bare (a writer's bytes must be what the next read
  decrypts). VeraCrypt payloads are not wrapped yet (see "What remains").
- App: `core::OpenOptions` (`src/core/include/drstein/core/opened.hpp`) gained
  `MasterKeys masterKeys`, `onUnlocked`, `cancelled`, `imagePassphrases` and
  `tcrypt = false`; `core::reprobe()` forwards them to `probe::Options` and
  wipes its copies (`core::wipeMasterKey` / `wipeMasterKeys`). `Workspace`
  keeps `m_masterKeys[sourceId][uuid]` for the process lifetime, hands it to
  every probe of that source (`Workspace::masterKeysFor(id)`), fills it from
  `onUnlocked` (merged on the GUI thread) and wipes it together with the
  source's passphrases (`closeImage`, disk vanished, `aboutToQuit`,
  destructor). The job card's Cancel reaches the KDF through
  `Progress::isCancelled()`: the tree stays (container locked), a toast "Unlock
  cancelled" is shown and the passphrase in flight is forgotten. Hot-plug
  refreshes, Refresh, repairs and Hex-write re-probes reuse the master key.

Tests: `test_core` 40 cases / 3498 assertions (Argon2 vectors on 1, 2, 3 and
default threads, one poll per slice, the memory cap; AES-XTS known answers and
random data on both paths, both key sizes, ragged block counts, in place; a
throughput case under `--no-skip -tc="*throughput*"`); `test_block` 14 / 670
(CachedDevice hits, eviction, partial reads, write invalidation, ragged last
chunk, 4 readers + a writer, also under TSan); `test_probe` 9 / 233 (a cached
key opens LUKS1/2 with no passphrase, a foreign key is ignored, a stale key
falls back, `onUnlocked` once with the oracle key, the payload is a
CachedDevice, cancellation at the first and at the third poll, an integrity
header → `luks.unsupported` with no child even with the right key, both copies
broken → `luks.header`); dr_stein `drstein_core_tests` 53 / 1797 (cached key,
stale key + passphrase, cancel at the first and at the third poll).

### 3. App behaviour (A, C) — done

- Wrong passphrase: `Workspace::unlock()` remembers the node (path + content
  uuid) and `finishPendingUnlock()` compares after the probe: a `Decrypted`
  child is selected silently; still locked → error toast "Wrong passphrase for
  <label>" and that passphrase is forgotten (bytes wiped).
  `Workspace::unlockLabel()` names the node ("partition 2 on disk5 (LUKS2)",
  "x.stein (encrypted image)") through `TopologyView.unlockRequested(what)`
  into `PassphraseDialog`, whose footnote says the passphrase is kept in memory
  for this source for this session and never written.
- VeraCrypt: `core::OpenOptions::tcrypt` defaults to false. The details card
  shows "Try VeraCrypt…" when `Workspace.tcryptCandidate` (an unrecognised
  region ≥ 256 KiB with no table, content or children), with a cost tooltip;
  `Workspace::tryVeraCrypt(passphrase)` arms a one-shot flag for the next
  probe; a failed trial toasts "Nothing unlocked".
- Unsupported containers: `core::isLocked()` is false for a node carrying the
  `luks.unsupported` note or whose header says `!supported`; `nodeDetails()`
  puts the reason into the issues once, where Unlock… would have been. Tested
  on a LUKS1 header patched to cbc-essiv:sha256 and on the real
  `luks1_cbc_essiv` fixture.
- Passphrases: image passphrases (`imagePassphrases`) and container
  passphrases are separate maps; `opened.cpp` tries every image passphrase in
  turn; both are forgotten on `closeImage`, when a disk vanishes, on
  `aboutToQuit` and in the destructor, each string overwritten before release.
- Mounts: `core::MountRequest { what, name, sourceId, nodePath, mountpoint }`
  and `core::mountDirectoryName(label, fallback)`: the mountpoint is
  `<tmp>/drstein-mounts/<label>` (sanitised, the node name as fallback,
  "-2" / "-3" when taken) and the NFS export is `127.0.0.1:/<label>`
  (`mount::NfsServerOptions::exportPath`), so Finder shows the label.
  `Workspace::mountSelected()` toasts with a "Reveal in Finder / Show in
  Explorer / Show in file manager" action (`Workspace.revealLabel`,
  `revealPath()`; `ToastHost` `actionLabel` + `actionPath`), the Mounts dialog
  rows have the same button, the Mount tooltip names NFS loopback / FUSE /
  WinFsp per platform (`Workspace.platformName`), and the details card and tree
  row show the app's own mountpoint through `MountManager::find(sourceId,
  nodePath)`.
- External unmounts: `mount::Mount::run()` returns when the mount disappears
  (macOS: `getfsstat` once a second, and at once when the kernel client's UMNT
  reaches the server, `NfsServerOptions::onUnmount`); `MountManager::reapEnded()`
  / `isMounted()` plus a 2 s watchdog in `MountsModel` drop the record and toast
  "Unmounted outside Dr Stein".
- Thread safety: `image::ImageDevice` and `MappedDevice::readAt` take a
  `std::mutex` (4-thread test in `test_image`); `XtsDevice` scratch is per
  device under a mutex (above); `Xts` has no mutable state.

Tests: `test_mount` 6 / 8727 (run() returned 951 ms after an external umount;
LUKS2 argon2id/512 and pbkdf2/4096 → ext2 → NFS mount listed and stat'ed
through the kernel, `f_mntfromname == 127.0.0.1:/inside_luks`; luks2_real's
ext4 over NFS with all six files hashed against `ext4_small.oracle.txt`, a
write refused, `MNT_RDONLY`); `test_image` 14 / 19110; `test_topology` cases
"LUKS2 through core::openSource" (locked / wrong / right / passphrase in the
wrong list), "a LUKS container that cannot be opened in process is not offered
for unlocking", "mount directories are named after the label". Scripted app
runs (`DRSTEIN_SCRIPT`) covered the wrong passphrase, the right one, Mount with
Reveal, Unmount from the dialog, an external `umount`, Try VeraCrypt on a 1 MiB
and a 128 KiB blob, and the relaunch landing.

### 4. Real disks (D) — relaunch done, fixtures partly done

- `relaunchElevated()` adds `DRSTEIN_SOURCE=<selected source id>` next to
  `DRSTEIN_VIEW`; `Workspace::restoreRelaunchSelection()` consumes it once at
  startup, selects that disk or image (waiting for main()'s own image probe
  when needed) and prompts once for the first locked container. Passphrases
  never enter the environment. Verified with image ids; the disk branch uses
  the same lookup but needs the elevation prompt to be driven by hand.
- Fixtures: `tests/fixtures/luks/` gained eight headers made with cryptsetup
  2.8.4 on the Ubuntu VM (passphrase "luks test passphrase"; the `generated=`
  line of each oracle records the command): `luks2_real` (cryptsetup defaults:
  argon2id t=10 m=1 GiB p=4, aes-xts-plain64 with a 512-bit key, 4096-byte
  sectors, two slots, token 0 `luks2-keyring` on slot 1, an ext4 `small_luks`
  whose files are listed in `ext4_small.oracle.txt`), `luks2_serpent`
  (serpent-xts-plain64), `luks2_aes128` (`--key-size 256`), `luks2_label`
  (`--label STEIN_LUKS --subsystem test`, header only), `luks2_detached_header`
  + `luks2_detached_data` (`--header`, t=9, payload at offset 0 of the data
  image), `luks1_aes128` (`--key-size 256`, pbkdf2-sha256 17 549 388
  iterations), `luks1_ripemd160` (747 914 iterations), `luks1_cbc_essiv`
  (refused by name, header only). The ext4 payloads were written through
  libstein's own `XtsDevice`, not by dm-crypt; `tools/fixtures/make_luks_fixtures.sh`
  does not make these eight yet.
- The LUKS stick for the hands-on test is still to do (below).

### 5. Tests — done for what can run on this Mac

`test_mount` mounts LUKS2 → ext2 and LUKS2 → ext4 over NFS and reads through
the kernel; `test_container` covers every header in the fixture directory (end
to end on the four old ones: open, wrong passphrase → Integrity, unlock,
payload probed as ext2 and listed through a Reader; 4096-byte sectors read past
the first sector by passphrase, by master key and by a bare XtsDevice; 1000
random windows cached against bare per fixture); `test_topology` has the app's
unlock chain. `STEIN_SLOW_TESTS=1` adds the passphrase unlocks of the real
fixtures (second slot, wrong passphrase on both slots, serpent, aes-128, label,
ripemd160, LUKS1 aes-128, the detached pair: about 30 s Release, 160 s Debug);
without it the suite still runs one real 1 GiB Argon2id unlock and checks every
payload through the oracle's master key. `STEIN_BENCH=1` enables the two timing
cases.

### Measured (Apple M1 Pro, 8 cores, 32 GiB)

Headers cryptsetup 2.8.4 produced on the Ubuntu VM; Release builds unless said.

| What | Measured |
|---|---|
| argon2id t=10, m=1 GiB, p=4 (luks2_real slot 0; cryptsetup benchmarks its slots to ~2 s) | 2.2 s wall, 7.0 s CPU over the lanes |
| serpent-xts-plain64 with the same KDF (luks2_serpent) | 2.2 s |
| aes-128-xts (luks2_aes128), the label-only header, the detached pair (t=9) | 2.4 s, 2.1 s, 2.2 s |
| second slot of luks2_real (slot 0 tried first and fails), and a wrong passphrase on both slots | 4.3 s each |
| LUKS1 pbkdf2-sha256, 17.5 M iterations (luks1_aes128; cryptsetup's own target was 2 s) | 7.4 s (7.1 s in the test): our PBKDF2 is ~3.5× slower than cryptsetup's |
| LUKS1 pbkdf2-ripemd160, 747 914 iterations | 1.7 s |
| cancellation after two Argon2 slices | 87 ms (433 ms Debug) |
| the four old fixtures (pbkdf2 1000 iterations, argon2id t=4 m=64 KiB) | 2–10 ms |
| the same unlocks in Debug (crypto core at -O0) | 10.4 s (defaults), 21.5 s (second slot), 44.8 s (17.5 M PBKDF2), 12.1 s (ripemd160) |
| argon2id t=4 m=256 MiB p=4 synthetic: one thread → lane threads | 0.738 s → 0.208 s (3.55×); 329 ms with 2 threads |
| `Xts::decryptSectors` over 64 MiB, AES, before → after | 855 → 4087 MiB/s (512-byte sectors), 865 → 4248 MiB/s (4096); portable AES 44 → 45 MiB/s |
| `XtsDevice::readAt` scratch | 2 × request size per call → at most 2 × 1 MiB per device |
| CachedDevice over the 1000 windows plus the file reads | luks2_real 2005 hits / 24 misses / 24 chunks of 256 KiB |
| app (Debug) on a real-cost LUKS2 image: unlock; re-probe with the cached key; Cancel to job end | 10.6 s; 1.3 s (the ext probe over XTS, no KDF); 214 ms |
| app: external unmount noticed | within the 2 s poll |
| app: Try VeraCrypt in Debug, one passphrase on a 1 MiB random blob | not finished after 20 min |
| `stein probe` on a 34 KB `.sparse` piece of a 2 TiB GPT disk (`RunListDevice`) | 0.03 s, 9.3 MB RSS |
| MBR repair of a 2.5 TiB extended partition with a lost logical (review fix) | 13.6 s → 0.44 s |

The app in a Debug build unlocked the real header within a minute and mounted
the ext4 under a mount point named after its label; the CLI listed, read and
mounted every variant and the detached pair.

### What remains

Library:
- VeraCrypt: `Tcrypt` payloads are not wrapped in `CachedDevice` (the
  one-liner from `luks.cpp:651` belongs after the `XtsDevice` in
  `tcrypt.cpp`); `TcryptOptions` has no `cancelled` hook, so a Cancel during
  "Try VeraCrypt…" takes effect after the current passphrase's full trial
  (minutes on a big region); a Release measurement of that trial and a cheaper
  trial order (fewer PRF candidates first) are open.
- PBKDF2 is ~3.5× slower than cryptsetup's (7.4 s for a 17.5 M-iteration
  LUKS1 slot): precompute the HMAC inner and outer states once per passphrase.
- Multi-slot retry cost: the second passphrase of a two-slot header pays two
  full KDFs; remembering which slot opened last per UUID would halve it.
- `UnlockOptions` has no thread-count knob (`Argon2Options::threads` is always
  min(lanes, hardware_concurrency)).
- A LUKS2 whose digest type is not pbkdf2 is refused (a guard; cryptsetup
  writes nothing else today). The header-copy scan tries the nine standard
  sizes ascending rather than the primary's `hdr_size` first.
- A `secureZero(std::span<std::byte>)` in `stein/core` (explicit_bzero /
  memset_s / volatile loop) so the key wipes in `luks.cpp`, `probeLuks` and the
  app resist dead-store elimination.
- A permanent XtsDevice / CachedDevice chunk-boundary test in `test_container`
  (payload > 1 MiB with windows across the 1 MiB chunk; the stress program
  lives in the perf agent's scratchpad only). The portable AES path gained
  nothing from the eight-block loop (byte-wise tables): a T-table or bitsliced
  AES is the next step if non-hardware targets matter. The throughput case stays
  `doctest::skip`.
- Debug builds: the crypto core at -O0 is 5–7× slower (1 GiB Argon2id 10.4 s
  against 2.2 s), so `test_container` grew by about 50 s in Debug;
  `$<$<CONFIG:Debug>:-O1>` on crypto.cpp / hash.cpp / cipher.cpp in
  `src/core/CMakeLists.txt` would fix the test runs and the app's Debug
  unlocks. `luks1_ripemd160`'s passphrase unlock sits under `STEIN_SLOW_TESTS`
  for this reason.
- `stein probe` has no `--header` (a detached-header volume shows as
  unrecognised data there); `probe::Options` would need a detached-header hook.
- The CLI has no automated test target; the `--header` chain, the prompt and
  `pt add --logical` placement were tested by hand and through a pty only.
- `test_mount`'s first case sets `STEIN_NFS_DEBUG=1` for the process, so the
  later cases print a trace line per request.
- `tools/fixtures/make_luks_fixtures.sh` does not generate the eight
  cryptsetup 2.8.4 fixtures; the commands are in the oracles' `generated=`
  lines and in the list below.

App:
- `PartitionEditor`, `ImageController` and `ToolsController` build their
  `OpenOptions` from `passphrasesFor(id)` only and pay the KDF on their own
  probes: add `o.masterKeys = Workspace::instance()->masterKeysFor(id)` at
  `partition_editor.cpp:49`, `image_controller.cpp:488` and
  `tools_controller.cpp:161`. Once they do, the probe's done-handler can forget
  the passphrase for every uuid that landed in the key cache (today the
  passphrase stays next to the key for their sake).
- A re-probe with a cached key costs about 1.3 s on the real-cost image: the
  ext probe over the XTS payload, to be re-measured with the read cache and the
  fast XTS path in place.
- `DRSTEIN_SOURCE` for a disk source and the elevated relaunch need a hands-on
  run (a LUKS disk and the elevation prompt); `main.cpp`'s header comment should
  list the variable.
- Hex edits and repairs inside containers and volumes still refuse to write
  back (`03-round-2-plan.md`, section N).
- The probe's `Node` payload devices hold the expanded cipher schedule for as
  long as the tree lives (library object).

Review fixes folded in alongside (MBR/GPT/FAT/exFAT and the app's logical
placement): `03-round-2-plan.md` section N carries the updated rules and what
they left open (MBR Outside/Loop breaks still close the chain when nothing is
found; grid corroboration needs a recognised filesystem; `MbrTable::addPartition`
rejects a busy logical index, so a logical inserted in front is numbered in the
preview rows only; the `stein repair` help still says "stale FAT copies"; the
Linux hands-on check of the "Partition table written" toast).

### Fixtures to generate on the Linux laptop (192.168.1.98)

All with cryptsetup ≥ 2.7, the passphrase in a key file (`printf '%s' 'luks
test passphrase' > pw.txt`, `--key-file pw.txt`), then `cryptsetup luksDump
--dump-master-key --batch-mode --key-file pw.txt` and `luksDump` into
`<name>.oracle.txt` / `<name>.dump.txt` the way
`tools/fixtures/make_luks_fixtures.sh` does, and `python3
tools/fixtures/sparsify.py` into `.sparse`. Where a filesystem is wanted, write
it through the kernel (`cryptsetup open … x && mkfs … /dev/mapper/x && mount …
&& cp … && umount && cryptsetup close x`) so the payload is dm-crypt's, not
ours, and put the file hashes in an `.oracle.txt` in `sha256sum` format like
`ext4_small.oracle.txt`. Extend `make_luks_fixtures.sh` with these rather than
running them by hand:

```sh
# 1. cryptsetup defaults (what GNOME Disks and the Ubuntu installer make): argon2id ~2 s, 1 GiB, p=4, 4096-byte sectors
truncate -s 64M luks2_default.img
cryptsetup -q luksFormat --type luks2 --key-file pw.txt luks2_default.img
# 2. 4096-byte sectors with data written by the kernel beyond the first 4 KiB (a few MiB of files, a directory tree)
truncate -s 64M luks2_4k.img
cryptsetup -q luksFormat --type luks2 --sector-size 4096 --key-file pw.txt luks2_4k.img
cryptsetup open --key-file pw.txt luks2_4k.img s4k && mkfs.ext4 -q -L small_luks /dev/mapper/s4k && mount /dev/mapper/s4k /mnt \
  && (cd /mnt && mkdir -p dir/nested && head -c 3M /dev/urandom > big.bin && echo hi > hello.txt && echo leaf > dir/nested/leaf.txt \
      && sha256sum big.bin hello.txt dir/nested/leaf.txt) && umount /mnt && cryptsetup close s4k
# 3. LUKS1 with a 256-bit key (AES-128-XTS) at the default 2 s iteration count
truncate -s 32M luks1_k256.img
cryptsetup -q luksFormat --type luks1 --key-size 256 --key-file pw.txt luks1_k256.img
# 4. two slots with different priorities plus a systemd-style token JSON on slot 1
truncate -s 64M luks2_slots_token.img
cryptsetup -q luksFormat --type luks2 --key-file pw.txt --keyslot-priority prefer luks2_slots_token.img
printf 'second slot passphrase' > pw2.txt
cryptsetup -q luksAddKey --key-file pw.txt --new-keyfile pw2.txt --new-key-slot 1 luks2_slots_token.img
cryptsetup config --key-slot 1 --priority ignore luks2_slots_token.img
printf '{"type":"systemd-recovery","keyslots":["1"]}' | cryptsetup token import luks2_slots_token.img
#    (with a TPM or a FIDO2 key instead: systemd-cryptenroll --tpm2-device=auto | --fido2-device=auto luks2_slots_token.img)
# 5. LVM inside LUKS: LUKS2 -> PV -> VG -> a linear LV with ext4 and a second LV with xfs
truncate -s 256M luks2_lvm.img
cryptsetup -q luksFormat --type luks2 --key-file pw.txt luks2_lvm.img
cryptsetup open --key-file pw.txt luks2_lvm.img lvmluks && pvcreate /dev/mapper/lvmluks && vgcreate steinvg /dev/mapper/lvmluks \
  && lvcreate -L 64M -n root steinvg && mkfs.ext4 -q -L lvm_root /dev/steinvg/root \
  && lvcreate -L 32M -n home steinvg && mkfs.xfs -q -L lvm_home /dev/steinvg/home \
  && vgchange -an steinvg && cryptsetup close lvmluks
# 6. a partitioned disk with a LUKS partition (GPT: EFI + LUKS; repeat with `label: dos` for MBR)
truncate -s 128M gpt_luks.img
sfdisk gpt_luks.img <<'EOF'
label: gpt
size=16MiB, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name=EFI
type=CA7D7CCB-63ED-4C53-861C-1742536059CC, name=crypt
EOF
LOOP=$(losetup -fP --show gpt_luks.img); cryptsetup -q luksFormat --type luks2 --key-file pw.txt ${LOOP}p2
cryptsetup open --key-file pw.txt ${LOOP}p2 gl && mkfs.ext4 -q -L on_gpt /dev/mapper/gl && cryptsetup close gl; losetup -d $LOOP
# 7. detached header: payload at offset 0 of the data device; one with --offset; a header backup of an on-device header
truncate -s 16M det_hdr.img; truncate -s 32M det_data.img
cryptsetup -q luksFormat --type luks2 --header det_hdr.img --key-file pw.txt det_data.img
truncate -s 16M det_hdr_off.img; truncate -s 32M det_data_off.img
cryptsetup -q luksFormat --type luks2 --header det_hdr_off.img --offset 4096 --key-file pw.txt det_data_off.img   # --offset in 512-byte sectors
cryptsetup luksHeaderBackup luks2_default.img --header-backup-file luks2_default.hdr                            # keeps the 2 MiB payload offset
# 8. serpent / twofish / camellia XTS, and argon2i
for c in serpent twofish camellia; do truncate -s 32M luks2_$c.img; cryptsetup -q luksFormat --type luks2 --cipher $c-xts-plain64 --key-file pw.txt luks2_$c.img; done
truncate -s 32M luks2_argon2i.img; cryptsetup -q luksFormat --type luks2 --pbkdf argon2i --key-file pw.txt luks2_argon2i.img
# 9. aes-cbc-essiv:sha256 LUKS1 (refused by name; header only), and the whirlpool / ripemd160 LUKS1 hashes
truncate -s 32M luks1_essiv.img; cryptsetup -q luksFormat --type luks1 --cipher aes-cbc-essiv:sha256 --key-size 256 --key-file pw.txt luks1_essiv.img
for h in whirlpool ripemd160; do truncate -s 32M luks1_$h.img; cryptsetup -q luksFormat --type luks1 --hash $h --key-file pw.txt luks1_$h.img; done
# 10. what must be refused on a real header: dm-integrity, an online reencryption in progress, a 4 MiB metadata area, a damaged primary copy
truncate -s 64M luks2_integrity.img; cryptsetup -q luksFormat --type luks2 --integrity hmac-sha256 --key-file pw.txt luks2_integrity.img
truncate -s 64M luks2_reenc.img; cryptsetup -q luksFormat --type luks2 --key-file pw.txt luks2_reenc.img; cryptsetup reencrypt --init-only --key-file pw.txt luks2_reenc.img
truncate -s 64M luks2_meta4m.img; cryptsetup -q luksFormat --type luks2 --luks2-metadata-size 4096k --key-file pw.txt luks2_meta4m.img
cp luks2_default.img luks2_broken_primary.img; dd if=/dev/zero of=luks2_broken_primary.img bs=4096 seek=1 count=3 conv=notrunc   # JSON of copy 0 wiped
```

Then a LUKS USB stick made the same way (GNOME Disks, or `cryptsetup
luksFormat /dev/sdX` with ext4 and a few files inside) for the hands-on test on
this Mac: the elevated relaunch with `DRSTEIN_SOURCE`, the passphrase prompt,
the label-named mount in Finder, and `sudo stein probe /dev/rdiskN`.

## Review round, 2026-10-24

An adversarial review of the LUKS commits found 27 candidates; every one that
was tried held. Fixed in libstein `9f1f1a1`-era commit (see git log) and the
app: a hostile header can no longer make the unlock allocate gigabytes or run
the KDF before the key size is checked (slots are settled at parse time with a
reason); a wiped primary LUKS2 header is detected from the secondary copy and
unlocks; `iv_tweak` counts 512-byte sectors as dm-crypt does; key material is
wiped after use (`stein/core/secure.hpp`); a bad sector no longer blacks out
the whole 256 KiB cache chunk around it; UTF-8 labels survive to the mount
name. App: Unlock and the passphrase dialog refuse while a job runs instead
of dropping the verdict in flight; a VeraCrypt volume found once stays found
across re-probes; the mount watchdog reads the mount table only; facades no
longer carry key copies they do not use; the dialog clears its field.
Still open from that round: VeraCrypt has no key hook or cancel in libstein;
cipher key schedules are not wiped on destruction; the app's details card
could say "header only" for a detached header opened alone.

