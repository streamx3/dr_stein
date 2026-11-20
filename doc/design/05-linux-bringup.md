# Linux bring-up

First build and run of Dr Stein and libstein on Linux, 2026-10-08. Everything
before this day had been compiled and run on macOS only; the Linux paths
(device enumeration, inotify watcher, frameless window with chips, pkexec,
`BLKZEROOUT`) were written blind. A second round on 2026-11-19 built the
November work there and made the Debian package (the last section).

## Machine

| | |
|---|---|
| OS | Linux Mint 22.3 (22.1 at the first bring-up; Ubuntu 24.04 base), kernel 6.8, x86_64 |
| Desktop | Cinnamon on X11 (Mint has no Wayland session worth testing yet) |
| Toolchain | GCC 13.3, CMake 3.28, Ninja (installed later the same day; the first pass used GNU make) |
| Qt | 6.12.0, online installer, `~/Qt/6.12.0/gcc_64`, with its documentation in `~/Qt/Docs/Qt-6.12.0` (6.11.2 at the first bring-up); the system's own Qt is 6.4.2 |
| Repo | `~/git/dr_stein` with the libstein submodule; fetch over HTTPS, push over SSH |
| Locale | `LANG=en_US`, `LC_NUMERIC=uk_UA`: sizes show a decimal comma, by design |

With `ninja-build` and `libfuse3-dev` installed the presets work as on macOS
(`QT_ROOT=$HOME/Qt/6.12.0/gcc_64 cmake --preset debug`). The first pass ran
without them, against Qt 6.11.2:

```bash
cmake -S libstein -B build/libstein-tests -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DSTEIN_BUILD_TESTS=ON
make -C build/libstein-tests -j14
cmake -S . -B build/debug -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.2/gcc_64
make -C build/debug -j14
```

The app binary is `build/debug/src/app/dr-stein` (at first it had the macOS
bundle executable's spelling, `Dr Stein`; the Linux install renamed it for
PATH). The scripted-run hooks work unchanged; from an SSH shell
set `DISPLAY=:0 XAUTHORITY=$HOME/.Xauthority` and the window opens in the
logged-in session:

```bash
DRSTEIN_VIEW=topology DRSTEIN_DELAY=4000 DRSTEIN_SCREENSHOT=$HOME/drstein-scratch/shot.png build/debug/src/app/dr-stein ~/drstein-scratch/composite.img
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
- The installed package, which needs sudo: see "Only the owner can check" at
  the end.

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

## 2026-11-19: the November work and the package

Done from the Mac over SSH, in a copy of the Mac tree synced to the laptop;
the owner's clone was not touched. Machine as above, Qt 6.12.0.

### The build

The November work (Browse's previews on worker threads, the column view, the
SVG time limit in `core::DeadlineRunner`) had been compiled on macOS only. On
the laptop it built with the `ci` settings (Debug, `DRSTEIN_WERROR=ON`,
Ninja): all 317 steps, every app and core file with `-Werror -Wshadow`, and
no warning at all. The C++ and the CMake needed no change.

What failed was qmllint. Qt 6.12's reports four warnings that Qt 6.10.2 on
the Mac does not, all of one kind, a property shadowed by one of the same
name:

- `id: window` on Main.qml's ApplicationWindow shadowed TopBar's and
  FrameResizer's own `window` property in `window: window`. Those two are now
  `hostWindow`. The `window` id stays, because the scripted-run recipes
  parent their timers on it.
- KeyValueGrid's `property var rows` shadowed GridLayout's `rows`. It is now
  `pairs`, and ConfirmDialog, TopologyView and BrowseView pass that.

qmllint is back at 0 warnings on both Qt versions. Configuring against Qt
6.12.0 prints one CMake warning that comes from the Qt install itself: its
QmlAssetDownloader plugin config looks for a Qt6TaskTree package that is not
installed. It does no harm.

Tests, GCC 13.3 and Qt 6.12.0:

- The core: 95 of 95 cases, 4344 assertions.
- libstein, built on its own with `STEIN_BUILD_TESTS=ON`: 12 of 13 test
  programs pass. `test_mount` fails 2 of its 6 cases (3 of 8712 assertions),
  all on one thing: `lost+found`, 0700 in the image, shows as 0555 where the
  test expects 0500. The two mount backends disagree. The NFS server that
  macOS uses strips the write bits (`nfs_server.cpp`), and that is what the
  test checks; the FUSE backend Linux uses also adds 0555 to every directory
  (`fillStat` in `mount_fuse.cpp`) so that it can be traversed. In October
  `test_mount` passed in the first pass, which built libstein without FUSE.
  Which rule
  is right is the owner's call (`doc/BACKLOG.md`); libstein is unchanged.
- Scripted X11 runs with previews on: Topology of the composite disk; Browse
  in columns and in the list layout, a long-named `.txt` previewed as text;
  a small FAT image with a JPEG (4000 × 2251), a PNG and an SVG, all three
  previewed through Qt's decoders, the SVG through the time-limited drawing.
  Every run exited 0 and printed no QML warning. A long name is cut in the
  middle and its column does not widen, as on macOS.

Not run on Linux in this round: real pointer and keyboard input (moving the
window by the top bar, double-click to maximise, the edge bands, the caption
buttons), mounting, the Hex, Image, Profiles, Tools and Partitions views, the
light scheme, Wayland.

### The package

Mint 22 and Ubuntu 24.04 ship Qt 6.4.2 and the app needs 6.7, so the package
carries its own Qt: the kit `QT_ROOT` names, from the Qt installer. It lives
in `/opt/dr-stein`, never on the system library path, so no other program
loads it and the app never loads the distribution's. The cost is that the
distribution does not patch it: the package has to be rebuilt when Qt
publishes a patch release (README).

The layout is the same for the `.deb` and for `cmake --install`
(`cmake/DrSteinLinuxInstall.cmake`); a plain install puts the files under
`/usr` here in `/usr/local` instead:

| Path | What |
|---|---|
| `/opt/dr-stein/bin/dr-stein` | the app, RUNPATH `$ORIGIN/../lib` |
| `/opt/dr-stein/bin/qt.conf` | `Prefix = ..`, from Qt's deployment |
| `/opt/dr-stein/lib/` | Qt's libraries, ICU and libstein's, RUNPATH `$ORIGIN` |
| `/opt/dr-stein/plugins/`, `qml/` | Qt's plugins and the QML modules the app imports |
| `/usr/bin/dr-stein` | a link to the binary |
| `/usr/share/applications/dr-stein.desktop` | the menu entry |
| `/usr/share/icons/hicolor/{scalable,symbolic}/apps/` | the icons |
| `/usr/share/man/man1/dr-stein.1.gz` | the manual page |
| `/usr/share/doc/dr-stein/` | `copyright`, `changelog.Debian.gz`, `qt-third-party.txt.gz` |

The binary is `dr-stein` on Linux (any Unix but macOS), so the debug build's
is `build/debug/src/app/dr-stein`; macOS and Windows keep "Dr Stein". The
`.desktop` file runs `dr-stein %F`, lists the disk-image MIME types and sits
in `System;Filesystem;`. The app sets its desktop file name to `dr-stein`,
so the window's app id (`_GTK_APPLICATION_ID`, `_KDE_NET_WM_DESKTOP_FILE`)
ties it to the menu entry and its icon.

How the tree is made:

- Qt's own deployment (`qt6_generate_deploy_qml_app_script`: qmlimportscanner,
  then the libraries, plugins and QML modules the binary needs, and
  `qt.conf`) writes into a staging folder in the build tree, which the install
  script then installs from its top scope. Files that Qt's deploy installed
  itself would be missing from `install_manifest.txt`, and so from the
  uninstall.
- Left out: Qt's translations, the QML debugger plugins, the eglfs and
  Wayland helper plugins, the generic input, network information and TLS
  plugins (these from Qt 6.10 on, where Qt's deploy can exclude plugin types),
  and every Qt Quick Controls style but Basic, which main.cpp sets before any
  QML loads. The only platform plugin is xcb.
- Qt's library symlink chains become one file named by the soname. libstein's
  libraries go in by their own install rules with RUNPATH `$ORIGIN`, also one
  file each: on Linux the app's CMake sets their VERSION to their SOVERSION
  (no libstein file changed). An install with `--strip`, and CPack, strip the
  libraries; the Qt kits' ICU comes unstripped.
- Run from the installed tree (a `qt.conf` next to `/proc/self/exe`), main.cpp
  unsets `QT_PLUGIN_PATH`, `QML_IMPORT_PATH` and `QML2_IMPORT_PATH`, which
  would load another Qt's plugins, and makes xcb the fallback of
  `QT_QPA_PLATFORM`; on Wayland the app runs through XWayland. With no X
  display it says so and exits 1 instead of aborting.
- Started through `/usr/bin/dr-stein`, Qt 6.12 reports the link as the
  application path, so `qt.conf` is not read; Qt finds the plugins and QML
  modules next to `libQt6Core` instead (the installer's kits are
  relocatable). The elevated relaunch hands the link's path to pkexec, which
  starts the same binary. A helper placed next to the app binary (08) has to
  be looked for through `/proc/self/exe`.

Dependencies are computed, not written by hand. CPack runs dpkg-shlibdeps over
every ELF file in the package, with `/opt/dr-stein/lib` as the package's
private library directory: the bundled Qt, ICU and libstein count as its own,
and only what they load from the system becomes Depends. On Mint 22.3 that is
43 packages: libc6 (>= 2.38), libstdc++6 (>= 13), X11 and fifteen libxcb
packages (libxcb-cursor0 among them), xkbcommon, OpenGL and EGL, fontconfig,
freetype, harfbuzz, GTK 3 and its stack (Qt's gtk3 platform theme), D-Bus,
libfuse3-3, zlib, zstd and a few more. Their `t64` names make it a package for
Ubuntu 24.04, Mint 22, Debian 13 and later. Recommends: `pkexec` (the
relaunch) and `fuse3` (`fusermount3`, which libfuse3 runs to mount without
root). There are no maintainer scripts: the dpkg triggers of
hicolor-icon-theme and desktop-file-utils are meant to refresh the icon cache
and the menu.

The version comes from git (`tools/package-deb.sh`): `+git<date>.<time>.<hash>`
after the tag `v<version>`, `~git…` before it, and `~dirty<UTC time>` for a
tree with uncommitted or untracked files, so each dirty build sorts above the
one before and below the clean build of the same commit. cpack refuses to run
except from the script, and only a tree configured with
`DRSTEIN_DESKTOP_PREFIX=/usr` (the `package` preset) has a package target.

`/usr/share/doc/dr-stein/copyright` is in Debian's machine-readable format,
made from `src/app/platform/copyright.in` and the repo's LICENSE files: MIT for
Dr Stein and libstein, CC-BY-4.0 for the Codicons (full text), LGPL-3, GPL-2
and GPL-3 for Qt (through `/usr/share/common-licenses`), and ICU's permission
notice. `qt-third-party.txt.gz` copies from Qt's documentation of that version
the attribution page of every third-party piece in the Qt modules the package
ships (`tools/qt-third-party-notices.py`); that is why packaging needs the
kit's documentation. Still missing: ICU's full LICENSE, whose third-party
part covers its dictionaries and time-zone data.

### Verified without root

The test package was
`dr-stein_0.1.1+git20261119.212341.becf8a3~dirty20261119.234923-1_amd64.deb`:
26.9 MB, Installed-Size 105,362 KiB, 143 entries, all root:root (directories
0755, files 0644, the binary 0755, one symlink). It came from an uncommitted
tree and has the placeholder Maintainer.

- `tools/package-deb.sh` as the normal user: in a fresh build directory and
  again in a used one, with Ninja and with Unix Makefiles (in a path with
  spaces), and with a relative `QT_ROOT`.
- The extracted tree: every ELF file has an `$ORIGIN`-relative RUNPATH, no
  file names the build user's home or the Qt kit, ldd finds nothing missing,
  and the system libraries the ELF files load come from exactly the 43
  packages in Depends. `desktop-file-validate` and `man --warnings` are clean.
  lintian is not installed there and was not run.
- Runs of the extracted tree in a cleared environment (`env -i` with HOME,
  DISPLAY, XAUTHORITY and a PATH holding a `dr-stein` link): Topology, Browse
  with PNG, JPEG and SVG previews, and the QML file dialog. `/proc/<pid>/maps`
  held no file from the Qt kit and none of the system Qt; of the styles only
  Basic was mapped. They also started with `QT_QPA_PLATFORM=wayland`,
  `XDG_SESSION_TYPE=wayland`, `QT_PLUGIN_PATH` and the QML import paths
  pointing at the system Qt 6.4.2, and `QT_QUICK_CONTROLS_STYLE=Material`.
  Without DISPLAY the app printed its message and exited 1. xprop showed the
  app id `dr-stein`.
- The relaunch, through a stand-in `pkexec` on PATH that runs its arguments
  without root: the second copy started from the tree, fell back to xcb and
  drew its window.
- `cmake --install` and `uninstall` under DESTDIR: the install writes the
  same 99 files and links as the package; the uninstall leaves nothing and
  removes only the directories the install created (a shared `/opt` holding
  another program stays). A Makefiles tree did the same round trip before the
  uninstall learned to record directories; a second uninstall only reported
  "Already gone". The prefix guard refuses `/`, `/usr` and `/usr/local` and
  writes nothing. The guard against an installed package was tried with a
  stand-in `dpkg-query`.
- The refusals: cpack outside the script, a source package, a build directory
  configured with another Qt, `DEBFULLNAME` without `DEBEMAIL`, a libstein
  without FUSE, a shallow clone.

### Only the owner can check (needs sudo, at the laptop)

1. `sudo apt install ./dr-stein_<version>_amd64.deb`: apt finds every
   dependency, and the triggers run without errors.
2. The menu entry: Dr Stein is in the menu with its icon and starts from
   there, and the panel shows the same icon for its window. Nemo's Open With
   offers it for a `.img`.
3. `dr-stein` from a terminal. The line "Failed to register with host portal
   … App info not found for dr-stein" that the uninstalled tree prints should
   be gone.
4. Elevation from the installed app: "Not elevated · images only" brings up
   the polkit dialog, and the elevated copy opens on the same view and source
   and lists the disks.
5. A mount from Browse, through fuse3.
6. `sudo apt remove dr-stein`: nothing is left of `/opt/dr-stein`,
   `/usr/bin/dr-stein`, the `.desktop` file, the icons, the manual page or
   `/usr/share/doc/dr-stein`, and the menu entry is gone. The settings in
   `~/.config/dr_stein/` stay, by design.
