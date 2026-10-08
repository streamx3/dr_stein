# Application icon

`dr-stein.svg` is the app icon (256 × 256 design grid, radial background, the
tree of three plates with the two lightning leads). `dr-stein-small.svg` is the
same drawing with bolder strokes for 32 px and below; `dr-stein-symbolic.svg`
is the monochrome symbolic variant for panels that recolour icons.

Where each one goes:

- In the binary: `dr-stein.svg` and `dr-stein-small.svg` are Qt resources
  (`:/icons/...`); `platform/app_icon.cpp` renders the small one at 16–32 px
  and the main one at 48–512 px into one `QIcon`, which `main.cpp` sets as the
  application window icon (taskbar, alt-tab, dock on every platform). Pixmaps
  rather than SVG files because Qt's SVG icon engine keeps a single file per
  mode and state.
- Linux install: `dr-stein.svg` to `share/icons/hicolor/scalable/apps`,
  `dr-stein-symbolic.svg` to `share/icons/hicolor/symbolic/apps`, and
  `../platform/dr-stein.desktop` to `share/applications`.
- macOS: `dr-stein.icns` becomes the bundle's `CFBundleIconFile`.
- Windows: `dr-stein.ico` through `../platform/dr_stein.rc`.

`dr-stein.ico` (16, 24, 32, 48, 64, 128, 256) and `dr-stein.icns` (16 … 1024)
were packed with Pillow from renders made with Qt's own `QSvgRenderer`, the
small variant for 16–32 px, so they look exactly as Qt draws the SVG. Regenerate
them the same way when the SVGs change.
