# Linux bring-up

First build and run of Dr Stein and libstein on Linux, 2026-10-08. Everything
before this day had been compiled and run on macOS only; the Linux paths
(device enumeration, inotify watcher, frameless window with chips, pkexec,
`BLKZEROOUT`) were written blind.

## Machine

| | |
|---|---|
| OS | Linux Mint 22.1 (Ubuntu 24.04 base), kernel 6.8, x86_64 |
| Desktop | Cinnamon on X11 (Mint has no Wayland session worth testing yet) |
| Toolchain | GCC 13.3, CMake 3.28, Ninja (installed later the same day; the first pass used GNU make) |
| Qt | 6.11.2, online installer, `~/Qt/6.11.2/gcc_64` |
| Repo | `~/git/dr_stein` with the libstein submodule; fetch over HTTPS, push over SSH |
| Locale | `LANG=en_US`, `LC_NUMERIC=uk_UA`: sizes show a decimal comma, by design |

With `ninja-build` and `libfuse3-dev` installed the presets work as on macOS
(`QT_ROOT=$HOME/Qt/6.11.2/gcc_64 cmake --preset debug`). The first pass ran
without them:

```bash
cmake -S libstein -B build/libstein-tests -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DSTEIN_BUILD_TESTS=ON
make -C build/libstein-tests -j14
cmake -S . -B build/debug -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.2/gcc_64
make -C build/debug -j14
```

The app binary is `build/debug/src/app/Dr Stein` (same spelling as the macOS
bundle executable). The scripted-run hooks work unchanged; from an SSH shell
set `DISPLAY=:0 XAUTHORITY=$HOME/.Xauthority` and the window opens in the
logged-in session:

```bash
DRSTEIN_VIEW=topology DRSTEIN_DELAY=4000 DRSTEIN_SCREENSHOT=$HOME/drstein-scratch/shot.png "build/debug/src/app/Dr Stein" ~/drstein-scratch/composite.img
```

## What it took

Less than expected. libstein compiled clean on GCC 13 at the first attempt
and all thirteen test binaries pass (image, block, platform, core, fs, mount,
pt, probe, layout, container, volume, ops, app). The app needed three fixes:

- `int64_t` is `long` on Linux and `QVariant` has no constructor for it
  (macOS has `long long`). The one place it bit, the OS error code in the
  error map, is now cast to `qlonglong`.
- Qt 6.11's CMake only looks at the first line of a `.js` file for
  `.pragma library`; ours sat under the licence comment and was re-evaluated per
  importing document. Moved to line 1.
- Policy QTP0004 set to NEW so Qt generates `qmldir` files for the QML
  subdirectories instead of warning.

One libstein test (`test_app`, the backup/restore scenario) failed on both
platforms: it still asserted "every byte written", which the skip-identical
default made false on purpose. The assertion now checks written + identical
covers the image and the write stays inside the damaged 2 MiB.

## Verified over SSH

- Core tests: 787 assertions pass.
- CLI `stein probe` on the exported composite disk: correct tree.
- The app opens in the X11 session with the frameless window and the custom
  chips, lists the NVMe disks under Internal and the device-mapper volumes under
  Virtual, and renders Topology, Hex, Browse, Image, Partitions and Tools the
  same as on macOS (screenshots in `doc/design/screenshots/linux-*.png`).
- Fonts fall back to Noto Sans and DejaVu Sans Mono; nothing overlaps.

## Not yet verified (needs a person at the laptop)

- Hot-plug: no USB stick was attached. Plug one in with the app open; the
  sidebar should update within half a second (inotify on `/dev` plus
  `/proc/self/mounts`).
- Elevation through `pkexec`: the polkit dialog appears on the desktop, so it
  must be exercised from a session on the laptop, not over SSH.
- Raw-device paths: `BLKZEROOUT`, `BLKDISCARD`, partition table re-read after
  an edit, restore onto a stick. All need root and the stick.
- Window chrome: drag by the title area, double-click to maximise, edge resize
  grips, and the chips' hover states. Also a Wayland session once Mint ships one.
- Drag-out from Browse into Nemo.

## Qt Creator

Qt Creator 20.0.2 from the online installer, same day. Two things kept the
"Desktop Qt 6.11.2" kit from working out of the box:

- The kit's compiler was Clang 18 (the ccache wrapper). The compiler entries on
  this machine were detected by Qt Creator 13 in 2025, before Creator 15 started
  ranking Clang below GCC on Linux; stored priorities are kept, so the tie went
  to the newer version number. Clang 18 on libstdc++ 13 has no `std::expected`
  (`__cpp_concepts` 201907 < 202002), and libstein's `Expected<T>` is exactly
  that: 121 errors in the core. Re-detecting the compilers in Preferences > Kits
  fixes future kits; the existing one needs GCC 13 chosen by hand. libstein now
  falls back to the bundled `tl::expected` on such toolchains and warns.
- `CMakePresets.json` makes the Configure Project page show only the
  preset-derived kits, and those resolve `$env{QT_ROOT}` against Qt Creator's
  own environment. Launched from the menu, that is unset, the system Qt 6.4.2
  is found instead and `find_package(Qt6 6.5)` fails. Setting `QT_ROOT` under
  Preferences > Environment > System, then Build > Reload CMake Presets, makes
  the preset kits usable; they pick `/usr/bin/c++` (GCC 13) and Ninja.

## GCC warnings

A clean Debug build of the whole tree (libstein, core, Qt layer, app) in
`build/warn` with GCC 13.3 produced no warnings at all.

## Open questions for the laptop session

- Whether to give the test user a `NOPASSWD` sudo rule limited to the test
  binaries so the raw-device tests can run unattended. Andrii's call.
- (Resolved the same evening: `ninja-build` and `libfuse3-dev` installed, so
  the FUSE mount backend is available once reconfigured.)
