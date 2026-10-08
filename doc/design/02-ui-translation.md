# From the Claude Design mockup to QML

The prototype is checked in under [`mockup/`](mockup/) (`Stein Disk Tool.dc.html`
plus the Nocturne design system it was built on). It is the specification the
QML is reviewed against, not a pixel contract: platform conventions (window
chrome, scrollbars, native dialogs) win where they conflict.

## Tokens → `Theme.qml`

The mockup's `applyTheme()` defines two palettes × two schemes as CSS variable
overrides. `Theme.qml` holds the same numbers:

| Mockup | Theme property | teal · dark | teal · light |
|---|---|---|---|
| `--color-bg` | `Theme.bg` | `#0c1619` | `#e9f3f4` |
| `--color-surface` | `Theme.surface` | `#132226` | `#f6fbfb` |
| `--color-text` | `Theme.text` | `#dff1f2` | `#10272a` |
| `--color-accent` | `Theme.accent` | `#3fc9c4` | `#22a39f` (ramp 600) |
| `--color-accent-2` | `Theme.accent2` | `#7ad3cf` | `#7ad3cf` |
| `--color-divider` | `Theme.divider` | text at 16 % | text at 16 % |
| `--color-accent-100…900` | `Theme.accentRamp[0…8]` | `ecfffd cdfaf7 a6f0ec 6fdfda 3fc9c4 22a39f 167c7a 105857 0b3a3a` | reversed |
| `--color-neutral-100…900` | `Theme.neutralRamp[0…8]` | `eef7f8 dceaec c3d6d9 a4b9bd 869a9f 687c81 4e5f63 374448 23303a` | reversed |
| `--shadow-sm/md/lg` edge | `Theme.edge1/2/3` | `24383d 375056 5f8389` | `c3d6d9 a4b9bd 687c81` |
| `--space-1…8` (0.7×) | `Theme.space1…8` | 2.8 5.6 8.4 11.2 16.8 22.4 px (rounded to 3 6 8 11 17 22) | |
| `--radius-sm/md/lg` | `Theme.radiusSm/Md/Lg` | 4 8 14 | |
| `--font-heading/body` | `Theme.fontFamily` | Inter if installed, else the system UI font | |
| `ui-monospace, Menlo` | `Theme.monoFamily` | Menlo / Consolas / DejaVu Sans Mono | |

The blurple palette stays available in Settings; teal is the default per the
owner's brief. "Light" and "Dark" follow `Settings.colorScheme`
(`system` reads `Qt.styleHints.colorScheme`). The sun/moon button in the top
bar toggles dark ↔ light; the gear opens Settings.

## Semantic colours (an addition to the design system)

Nocturne is a mono scheme: the mockup colours OK, warnings and even the
"destructive" tag from the accent ramp. A disk tool must not. The library's
error model says `Integrity → red, never silent`, and a dirty NTFS flag must
read as a warning at a glance. `Theme` therefore adds three roles tuned to
each scheme and used only for status:

| Role | Use | dark | light |
|---|---|---|---|
| `Theme.ok` | health dots, "complete", "verified" | accent ramp 400 | accent ramp 600 |
| `Theme.warning` | warnings, dirty volumes, "pattern stops at", unreadable sectors | `#e0b35a` | `#9a6b00` |
| `Theme.danger` | errors, destructive tags, erase confirmations | `#e37272` | `#b23a3a` |

Everything else keeps the mono rule: accent as lines, glows and marks, never
as a flood.

## Layout mapping

| Mockup region | QML |
|---|---|
| 1280×800 frame, 48 px top bar, 264 px sidebar | `ApplicationWindow` 1280×800 (min 960×620), `TopBar` 48 px, `SplitView` with a 264 px sidebar (user-resizable, 200–400) |
| Top bar view buttons with a 2 px bottom mark | `ViewTabs` (Repeater of `TabButton`-like items) bound to `Workspace.view` |
| Elevated / not-elevated tag, Refresh | `StatusTag` + `StButton`; plus the two added icon buttons (theme, settings) |
| Sidebar groups (uppercase 10 px labels) and device rows (icon · name · size · subtitle, 2 px accent left edge when selected) | `ListView` with `section.property: "group"` over `SourceListModel`; `SourceRow` delegate |
| "Open image file…" | `StButton` → `FileDialog` (QtQuick.Dialogs) |
| Identity strip (title, mono path, identity line, tags: table, health, read-only) | `IdentityStrip` bound to `Workspace.current` |
| Topology: 34 px proportional bar, tree table (1fr 150 90 96), 320 px details card | `SegmentBar`, `ListView` with `GridLayout` rows over `TopologyModel`, `DetailsCard` |
| Hex: segmented struct picker, hex pane (76 / 16 cells / 136) and parsed pane | `StructInspector`: `StSegmented`, `HexPane` (`ListView` of 16-byte rows with a `Row` of 16 cells), `FieldsPane` |
| Browse: 220 / 1fr / 280 panes | `TreeView` over `DirTreeModel`, `ListView` over `EntriesModel`, `PreviewCard` |
| Image: form (2 columns) + progress/report card | `ImageForm`, `JobCard` (shared with every long job) |
| Profiles: 3-column cards + dashed "New profile" | `GridLayout` of `ProfileCard`, `Flow` fallback below 1100 px |
| Tools: scan card with a 64-column cell grid, destructive card, confirm dialog | `ScanGrid` (`Grid` of `Rectangle`s from `scanCells`), `ConfirmErase` dialog with the typed kernel name |
| Partitions: current/preview bars, preview tree, operation stack card | `PartitionsView` |

Icons: the mockup draws inline SVG strokes on `currentColor`. `Icon.qml`
takes a name and a colour and renders the same path data through an SVG
`data:` URL (QtSvg), so icons recolour with the theme; new icons (gear,
sun, moon) follow the same stroke style (24-unit grid, 1.6 stroke, round
caps). Phosphor's shapes are used as the reference where the mockup has
none.

## Deviations, on purpose

- Semantic colours above.
- Native `FileDialog`/`FolderDialog` instead of the mockup's "Choose…"
  stubs.
- The confirm dialog for the capacity test is modal to the window, not an
  overlay inside the Tools pane, so it cannot be hidden behind a scroll.
- The hex editor's "Write N bytes…" asks a second time with the device
  identity and the dirty ranges; the mockup only had the button.
- Report rows come from `stein::Report` after a job completes; while it runs,
  the card shows the live phase, progress and the library's messages.
- The sidebar shows a fourth group, "Virtual", for loop/dm/md devices, so
  nothing the OS lists is hidden (GNOME Disks hides them; libstein tags them).

## Additions after the first review (2026-10-07)

**Palettes.** Besides the mockup's teal and blurple there are two more, picked in
Settings: `blue` (Apple's system blue and greys, for sitting next to Finder) and
`grey` (no hue at all; the accent is a lighter grey, so status colours are the
only colour left). The token structure is unchanged; each palette is one entry in
`Theme.palettes`.

**Client-side title bar.** On by default ("Draw the title bar in the app" in
Settings, applies at the next launch; `DRSTEIN_CHROME=native` for one run):

- macOS and Windows: `Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint`
  (Qt 6.9+). The system keeps its frame, resizing, zoom, Stage Manager and
  full-screen; the client area extends under the title bar. On macOS a small
  Objective-C++ helper (`src/app/platform/window_chrome_mac.mm`) attaches an
  empty unified `NSToolbar` so the traffic lights sit centred in a 52 px band,
  hides the title text, and reports the lights' frame; the tab bar takes its
  height and left inset from that measurement rather than from constants.
  ApplicationWindow keeps its content under the safe area by itself, so
  `Main.qml` pulls the layout back up by `SafeArea.margins.top`.
- Windows: Qt draws the frame; the caption buttons are ours (`WindowControls`,
  46 px chips, close turns red on hover). Snap Layouts on hover over the
  maximise chip is a known gap with this approach (it needs `WM_NCHITTEST`
  returning `HTMAXBUTTON`); the double-click-to-maximise and drag work through
  `startSystemMove`. Not yet run on Windows.
- Linux: `Qt.FramelessWindowHint`, `startSystemMove` for the bar and
  `startSystemResize` from 6 px edge bands (`FrameResizer`); no compositor
  shadow. The caption buttons are VS Code's: 46 px wide, the full 48 px of the
  bar, Microsoft's Codicon glyphs at 16 px (`src/app/icons/codicons/`, CC-BY
  4.0, recoloured by `CodiconProvider`), a 10 % hover overlay, close turns
  `#e81123` with a white glyph. Before this the chips were the Windows ones,
  36 px tall and top-aligned, so their centres sat 6 px above the rest of the
  row. Brought up on Linux 2026-10-08 (`05-linux-bringup.md`).

**Layout rule learned.** Qt Quick Layouts never shrink below the children's
minimum, and an item without `Layout.fillWidth` has minimum = implicit width.
A long path in a text field or a long checkbox sublabel therefore pushed a
dialog's buttons out of the popup. Every form control now declares
`Layout.fillWidth: true; Layout.minimumWidth: 0` for itself (`StCheck`,
`StTextField`, `StCombo`) and wrapping texts in dialogs do the same.

**Progress bar.** The indeterminate slide animated `x` directly, so a bar that
went determinate kept the last offset. The slide is now a separate property that
only applies while indeterminate.

**Elevation.** "Not elevated · relaunch…" in the top bar (and the button on the
"Needs elevation" screen) starts a second copy through the OS prompt: macOS
administrator authorisation via `osascript … with administrator privileges`,
`pkexec` on Linux (display variables handed back), UAC `runas` on Windows. The
open images, palette, scheme, title-bar choice and profiles folder travel in
the environment (`DRSTEIN_*`), since root has its own settings. Verified on
macOS. Caveat found on macOS 26: root is not enough for raw disks; TCC's Full
Disk Access / Removable Volumes grant is attributed to the launching app, so a
copy started from an IDE or from Claude fails with "Operation not permitted"
while one started from Terminal (which has the grant) works. The app says so
when it is already root.

**Full Disk Access (macOS).** Opening a raw disk fails with `EACCES` when the user
id may not open the node (fix: elevation) and with `EPERM` when the privacy
system vetoed it (fix: Full Disk Access; root does not bypass it, and the grant
belongs to the app that started the process, so a terminal launch uses the
terminal's grant). The "Needs elevation" screen now tells the two apart and,
for the privacy case, offers **Open Privacy & Security…**
(`x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles`),
**Show app in Finder** (so the bundle can be dragged into the list) and **Try
again**. Verified that the link brings System Settings to the front on macOS 26.

## Round 2 (2026-10-08)

See `03-round-2-plan.md` for the item list. Rules that came out of it:

- **Tables are fixed-width columns.** A `Text` in a `RowLayout` with only
  `Layout.preferredWidth` still grows with its text; a column needs
  `minimumWidth = maximumWidth = preferredWidth` and `elide`, and the flexible
  column needs `Layout.minimumWidth: 0`. Applied to Browse, Topology, Partitions
  and `ColumnHeader`.
- **The restore target is red.** Whatever the sidebar marks as the restore target
  (device or raw image) gets the danger colour in every scheme: tinted row, red
  bar, red name, a "restore target" tag. The mark moves with the choice and clears
  after the restore, when the image changes, or when the Image view leaves
  Restore mode. The target list is every disk and raw image (the open one
  included); only the image being restored is excluded. Picking an image file in
  the Restore form opens it as a source like "Open image file…" does.
- **Device column.** Target rows start with the OS device in a mono font, padded
  by the model to one width (`/dev/disk4`, `/dev/sda`, `file`), then the name in
  the regular font. `StCombo.monoRole` renders it in the list and in the closed
  combo.
- **Writes lock the UI.** While a job runs (anything but a probe or a usage
  read), tabs, sidebar and forms are disabled and dimmed; the job card with
  Cancel stays live. Expert mode exposes "Allow using the app while a read or
  write runs" with the warning spelled out.
- **Passphrase before the typed confirmation.** For an encrypted image the
  passphrase is asked first and checked against the key area; a wrong one is
  reported and asked again, with no delay. Only then comes the device-name
  confirmation, then the write.
- **OS device ids.** Partition rows show `disk5s2` / `sda1` / `nvme0n1p1` next to
  "Partition 2" and a "mounted" tag when the OS has them mounted; the details
  card lists `device` and `mounted at`, and the Mount button becomes "Unmount …"
  for OS mounts (through libstein's `Platform::unmount`).
- **Environment is a default, not a lock.** `DRSTEIN_PALETTE` / `DRSTEIN_SCHEME`
  seed the settings at start (the elevated relaunch uses them) and the Settings
  dialog can change them afterwards.
