# qtheme: the theme file format

Dr Stein ships four palettes (teal, blurple, blue, grey), each in a dark and a
light scheme. Today they live as a table inside `src/app/qml/Theme.qml`;
everything the UI paints is derived from that table by a handful of rules.
`qtheme` is that table, its rules and a few additions (background gradients
under any angle, SVG backgrounds, inheritance) written down as a file format,
so themes can be authored, shared and loaded without touching the app.

This note defines version 1 of the format. The app does not load it yet; the
bundled themes under `themes/` are exported from Theme.qml and checked against
it by `tools/qtheme_check.py`, so the two cannot drift apart until the loader
lands. The checker is also the reference implementation of every rule below:
where this prose and the checker disagree, one of them has a bug.

## 1. The file

- A theme is one UTF-8 JSON document (RFC 8259) with the extension `.qtheme`.
  JSON because Qt parses it natively (`QJsonDocument`) and QML can read it
  without a dependency. There are no comments in JSON; use `description` and
  `notes` for prose.
- The file is named after its `id`: the theme `aurora` lives in
  `aurora.qtheme`. Assets (SVG backgrounds) sit beside it or in a
  subdirectory and are named by relative path (§7.3). Version 1 has no
  directory or archive form.
- Identifiers (`id`, `inherits`, variable names) match `[a-z][a-z0-9-]*`.
- A theme that does not `inherits` must give `qtheme`, `id`, `name`,
  `accent`, both `ramps` and both `schemes` with their six base colours.
  Everything else is optional and has a default. A theme that `inherits` may
  omit anything the parent already gives.
- Forward compatibility: `qtheme` is the format's major version. A loader
  refuses a file whose `qtheme` is larger than the version it knows. Within a
  version, a loader ignores keys it does not know and says so in its log; the
  checker rejects them, so a file that passes the checker contains only what
  this note defines.

Minimal complete theme:

```json
{
  "qtheme": 1,
  "id": "teal",
  "name": "Teal",
  "accent": { "dark": "#3fc9c4", "light": "#22a39f" },
  "accent2": "#7ad3cf",
  "ramps": {
    "accent":  ["#ecfffd", "#cdfaf7", "#a6f0ec", "#6fdfda", "#3fc9c4", "#22a39f", "#167c7a", "#105857", "#0b3a3a"],
    "neutral": ["#eef7f8", "#dceaec", "#c3d6d9", "#a4b9bd", "#869a9f", "#687c81", "#4e5f63", "#374448", "#23303a"]
  },
  "schemes": {
    "dark":  { "bg": "#0c1619", "surface": "#132226", "text": "#dff1f2", "edge1": "#24383d", "edge2": "#375056", "edge3": "#5f8389" },
    "light": { "bg": "#e9f3f4", "surface": "#f6fbfb", "text": "#10272a", "edge1": "#c3d6d9", "edge2": "#a4b9bd", "edge3": "#687c81" }
  }
}
```

That is the whole of today's teal theme. The rest of this note is what else a
file may say, and exactly how the UI's colours follow from it.

## 2. Top level

| key | type | meaning |
|---|---|---|
| `qtheme` | integer | format version, `1` |
| `id` | identifier | stable id; the file name, the settings value and the target of `inherits` |
| `name` | string | shown in the settings dialog |
| `description`, `author`, `license`, `version`, `notes` | string | metadata, free text; not inherited |
| `inherits` | identifier | another theme this one overlays (§8) |
| `vars` | object | named colours, referenced as `$name` (§3) |
| `accent` | colour or `{dark, light}` | the brand colour. A bare colour applies to both schemes; the pair gives each scheme its own |
| `accent2` | colour or `{dark, light}` | the secondary accent (gradient ends, the title glyph); default `accent.300` |
| `ramps` | object | `accent` and `neutral`, nine colours each, lightest first (§4) |
| `schemes` | object | `dark` and `light` (§5) |
| `roles` | object | role overrides shared by both schemes (§5) |
| `partitions` | object | swatches for the disk map (§6) |
| `background` | object | window background shared by both schemes (§7) |
| `typography` | object | font families, sizes and heading weight (§9) |

## 3. Colour values

A colour is a string in one of these forms:

- `"#rgb"` or `"#rrggbb"`. There is no `#rrggbbaa`; alpha is always written
  with the suffix below, so a reader never has to guess which end of the
  string the alpha is on (CSS puts it last, Qt first).
- `"transparent"`, which is `#000000` at alpha 0. It takes no alpha suffix.
- A ramp step: `"accent.500"`, `"neutral.300"`. Only the nine steps 100,
  200 … 900 exist (§4).
- A role name: `"text"`, `"surface"`, `"edge2"`, `"accent"`… any role of §5,
  resolved in the scheme the value is used in.
- A variable: `"$brand"`, defined in `vars`.
- Any of the above except `transparent` with an alpha suffix: `"text@16%"`
  (0% to 100%, integers) or `"text@0.16"` (0 to 1). The suffix sets the
  alpha; it does not multiply an existing one.

References resolve in the scheme being computed, so one shared definition can
mean different colours in dark and light. There is no way to name the other
scheme's colour; use a variable or a literal. Reference chains end in a
literal or `transparent`; a cycle through roles or variables is a validation
error. Every variable in `vars` must resolve in both schemes, whether or not
something uses it.

## 4. Ramps and contrast steps

`ramps.accent` and `ramps.neutral` are nine physical colours from lightest to
darkest. Each entry is a `#rrggbb` literal: no short form, no alpha, no
references. The UI never indexes a ramp by position; it asks for a *contrast
step*, where 100 is the colour farthest from the scheme's background and 900
the one closest to it:

| physical index (lightest first) | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| step in the dark scheme | 100 | 200 | 300 | 400 | 500 | 600 | 700 | 800 | 900 |
| step in the light scheme | 900 | 800 | 700 | 600 | 500 | 400 | 300 | 200 | 100 |

This is why one ramp serves both schemes, and why `"neutral.500"` written once
in `partitions` or `roles` is right in both. Only the nine steps exist;
`accent.150` or `accent.1000` is a syntax error.

## 5. Schemes and roles

`schemes.dark` and `schemes.light` each carry the six base colours and may
carry overrides for any derived role and a scheme-specific `background`:

```json
"schemes": {
  "dark": {
    "bg": "#0c1619", "surface": "#132226", "text": "#dff1f2",
    "edge1": "#24383d", "edge2": "#375056", "edge3": "#5f8389",
    "roles": { "warning": "#e0b35a" },
    "background": { "gradient": { "angle": 160, "stops": [ { "at": 0, "color": "bg" }, { "at": 1, "color": "accent.900@70%" } ] } }
  },
  "light": { "...": "..." }
}
```

The roles, with the rule each one follows when not overridden. The first
eight are set directly (base colours in the scheme, `accent` and `accent2` at
the top level) and never appear in `roles`. The rest are derived; the rules
are the ones Theme.qml applies today, and a loader must reproduce them
exactly.

| role | default | used for |
|---|---|---|
| `bg` | scheme base | window background |
| `surface` | scheme base | cards, panels, hovered and selected rows |
| `text` | scheme base | body text |
| `edge1`, `edge2`, `edge3` | scheme base | borders from faint to strong |
| `accent` | top-level `accent` (the scheme's member of a pair) | focus rings, primary buttons, the current tab |
| `accent2` | top-level `accent2`, else `accent.300` | secondary accent |
| `divider` | `text@16%` | hairlines |
| `textBright` | `neutral.200` | emphasised text |
| `textSoft` | `neutral.400` | secondary text |
| `textMuted` | `neutral.500` | captions, column headers |
| `textDim` | `neutral.600` | the quietest text |
| `sunken` | `neutral.900` | inset panels, code, free space |
| `hover` | `surface` | hovered rows |
| `selectedRow` | `surface` | the selected row |
| `selectedTint` | `accent.900` | selection wash, the current item |
| `link` | `accent.300` | links, hovered icons, primary button text, dirty values |
| `selectionBg` | `accent.700` | text selection in fields |
| `selectionText` | `accent.100` | text inside a selection |
| `tagFill` | `neutral.800` | neutral tag background |
| `tagText` | `neutral.100` | neutral tag text |
| `scrim` | dark: `#000000@55%`; light: `#000000@30%` | the veil behind modal dialogs |
| `ok` | dark: `accent.400`; light: `accent.600` | healthy state |
| `warning` | dark: `#e0b35a`; light: `#9a6b00` | warnings |
| `danger` | dark: `#e37272`; light: `#b23a3a` | errors, destructive actions |
| `info` | `textSoft` | informational health |

The roles from `link` to `scrim` are not properties of Theme.qml yet; the
views compute them from ramp steps today. They are roles here so a theme can
move them, and the loader will route the views through Theme.qml when it
lands.

Overrides may be given in the top-level `roles` (both schemes) or in a
scheme's `roles` (that scheme only; wins over the top level). An override may
reference other roles; order of definition does not matter, cycles are errors.

## 6. Partition swatches

The disk map colours partitions by cycling through a list of swatches, each a
fill with a matching text colour; free space has its own swatch. The defaults
reproduce Theme.qml:

```json
"partitions": {
  "swatches": [
    { "fill": "accent.800",  "text": "accent.100" },
    { "fill": "accent.700",  "text": "accent.100" },
    { "fill": "neutral.800", "text": "textBright" },
    { "fill": "neutral.700", "text": "textBright" },
    { "fill": "accent.600",  "text": "accent.100" },
    { "fill": "neutral.600", "text": "textBright" }
  ],
  "free": { "fill": "sunken", "text": "textMuted" }
}
```

`swatches` has at least one entry; partition `i` takes entry `i mod length`.
Being an array, `swatches` replaces the inherited list as a whole (§8).

## 7. Background

A background is painted behind the whole window, under the sidebar and the
content, in three layers: a flat colour, an optional gradient, optional
images. Panels and cards stay opaque; the background shows in the gaps, the
sidebar and the empty states.

It may be given once at the top level, per scheme, or both. The effective
background of a scheme is the top-level block overlaid by the scheme's block,
key by key (the same merge as §8), so a shared image can be kept while each
scheme sets its own gradient. `null` removes a layer that a parent or the
top level gave.

```json
"background": {
  "color": "bg",
  "gradient": {
    "type": "linear",
    "angle": 135,
    "stops": [
      { "at": 0.0, "color": "bg" },
      { "at": 0.6, "color": "bg" },
      { "at": 1.0, "color": "accent.900@80%" }
    ]
  },
  "image": {
    "src": "aurora-wave.svg",
    "fit": "cover",
    "anchor": "bottom right",
    "opacity": 0.18,
    "tint": "accent",
    "blend": "normal"
  }
}
```

### 7.1 Colour layer

`color` defaults to the role `bg`. It is what the OS sees as the window
colour (title bar, resize flashes) and what shows where the other layers are
transparent or missing, so it must be opaque: a `color` that resolves to
alpha below 1 is a validation error.

### 7.2 Gradient layer

- `type`: `"linear"` (default) or `"radial"`. Keys that belong to the other
  type are errors, so a stray `angle` on a radial gradient is caught rather
  than ignored.
- `angle` (linear): degrees, CSS convention. `0` paints from bottom to top,
  `90` from left to right, `180` from top to bottom, clockwise from there. Any
  real number is allowed; the default is `180`. The gradient line passes
  through the centre of the painted area and is exactly long enough for the
  first and last stop to touch the two corners the angle points at, as CSS
  `linear-gradient` does: its length is `|w·sin a| + |h·cos a|`. An
  implementation that only has vertical and horizontal gradients (plain Qt
  Quick `Gradient`) must use a shape gradient or a shader for other angles,
  and may fall back to the nearest of 0, 90, 180, 270 only if it logs that
  it did.
- `center` and `radius` (radial): the gradient is a circle. `center` is
  `[x, y]` as fractions of the painted area's width and height, default
  `[0.5, 0.5]`; `radius` is a fraction of the area's longer side, default
  `0.75`. Both may exceed 1.
- `stops`: two or more `{ "at": 0..1, "color": colour }`, `at` non-decreasing.
  Colours may carry alpha; the gradient composites over the colour layer.
  Colours interpolate in premultiplied sRGB, which is what Qt does, so a fade
  to `transparent` does not pass through grey.
- `space`: `"window"` (default) paints over the whole window; `"content"`
  paints over the content area only, which is the window minus the sidebar.
  Both the angle geometry and the radial fractions use the chosen area.

### 7.3 Image layer

`image` is one object or a list of objects painted in order, each with:

- `src`: a path to an SVG file. Rules: relative, forward slashes only, no
  `..`, not starting with `/`, ending in lowercase `.svg`, and inside the
  theme's directory. It resolves relative to the file that names it, so a
  child theme that inherits a parent's image keeps the parent's file; a
  loader turns `src` into an absolute path when it reads the file. Only SVG
  in version 1, so the image stays crisp at any size and can be tinted.
- The SVG must be self-contained: a well-formed XML document whose root is
  `<svg>` in the SVG namespace with a `viewBox`, no `DOCTYPE`, no `<script>`,
  `<image>` or `<foreignObject>`, no event attributes (`onload` and friends),
  and every `href` or `url()` pointing at a `#` fragment inside the file.
  Files above 1 MiB are rejected. The natural size of the image is its
  `viewBox` in logical pixels; a HiDPI screen gets it rasterised at device
  resolution.
- `fit`: `"cover"` (default; fill the area, crop), `"contain"` (fit inside,
  letterbox), `"stretch"` (fill the area, ignore aspect), `"tile"` (repeat at
  natural size), `"none"` (natural size).
- `anchor`: where the image sits when it does not fill the area and which
  part survives cropping when it does: `"center"`, `"top"`, `"bottom"`,
  `"left"`, `"right"` or a vertical-then-horizontal pair: `"top left"`,
  `"top right"`, `"bottom left"`, `"bottom right"`. Default `"center"`.
- `offset`: `[x, y]` in logical pixels added after anchoring, default `[0, 0]`.
- `scale`: multiplier applied after `fit`, default `1`.
- `opacity`: 0..1, default `1`.
- `tint`: a colour; every `currentColor` in the SVG resolves to it before
  rendering, so one drawing can follow both schemes. Default: the scheme's
  `text` role. Nothing else in the SVG is recoloured. If the tint carries
  alpha, that alpha is multiplied into `opacity` rather than written into the
  SVG, because SVG `currentColor` carries no alpha.
- `blend`: `"normal"` (default), `"multiply"`, `"screen"`, applied against
  the layers below. An implementation without blend modes paints `normal`
  and logs that it did. `screen` on a light background and `multiply` on a
  dark one barely show; the example themes use them the other way round.
- `space`: as for gradients.

A theme may give `image` without `gradient` and the other way round.

### 7.4 Faults

A theme file that fails validation (structure, references, cycles, an
unresolvable `inherits`) is rejected as a whole; the app logs why and falls
back to the bundled `teal`. Inside a theme that loads, a background layer
that cannot be painted does not take the theme down:

- an unreadable, oversized or non-conforming SVG: that image is skipped;
- a gradient the implementation cannot paint: the gradient is skipped;
- the colour layer is always painted.

Each skip is logged once. The order is the fallback chain: image → gradient →
colour → `bg`.

## 8. Inheritance

`"inherits": "teal"` builds the theme by overlaying this file on the resolved
parent:

- Objects merge key by key, at every depth: `schemes`, `roles`, `background`
  and its `gradient` and `image` objects, `partitions`, `typography`, `vars`.
- Arrays and scalars replace. `ramps.accent` is therefore all nine entries or
  nothing; so are `swatches`, `stops`, `family` and a list-valued `image`.
- A key set to `null` removes the inherited value. `null` is allowed wherever
  the key is optional (it is not allowed for `qtheme`, `id`, `name`, a stop's
  `at` or `color`, an image's `src`, a gradient's `stops` or a swatch's
  members). A theme without a parent may also write `null`; it means absent.
- `description`, `author`, `license`, `version` and `notes` are not
  inherited; a child says its own or has none.
- `vars` merge like any object and references resolve after merging, so a
  child that redefines `$k` changes every place the parent used it.
- Asset paths were resolved when the parent was read (§7.3).

Chains may be several deep; a cycle is an error. The parent is the theme
whose `id` matches, found by its file name: the bundled themes are searched
first, then the user's theme directory. Bundled ids are reserved; a user file
that reuses one is ignored with a log message, so a bundled theme can never
be shadowed by accident.

This is what makes a seasonal variant a ten-line file: inherit `teal`, change
`accent`, add a background.

## 9. Typography

Optional; defaults to the app's values. It exists so a theme can be complete,
not because most themes should touch it.

```json
"typography": {
  "family": ["Inter"],
  "mono": ["JetBrains Mono", "Menlo", "Consolas", "DejaVu Sans Mono"],
  "sizes": { "body": 13, "small": 12, "tiny": 11, "micro": 10, "h1": 18, "h2": 16, "h3": 15 },
  "headingWeight": 500
}
```

`family` and `mono` are preference lists: the first installed family wins,
checked with `Qt.fontFamilies()`. If none is installed the app's own choice
stands: the system UI font for `family`; Menlo on macOS, Consolas on Windows
and DejaVu Sans Mono elsewhere for `mono`. Sizes are logical pixels from 1
to 512. `headingWeight` is a CSS-style weight in steps of 100 (400 regular,
500 medium, 600 semibold, 700 bold).

Spacing, radii, control heights and animation timing are not themeable: they
are layout, and a theme that changes them breaks every view's alignment.

## 10. Validation

`tools/qtheme_check.py` checks files or directories of themes:

- structure and types as above, unknown keys, identifiers, ramps of exactly
  nine `#rrggbb` literals, `id` equal to the file name;
- every colour parses, every reference and every variable resolves in both
  schemes, no cycles;
- `inherits` resolves and the merged result is complete; the effective
  background of each scheme is well formed, its `color` opaque, its SVGs
  present, self-contained and under 1 MiB;
- contrast, as advice rather than errors (`--strict-contrast` makes them
  errors): `text` on `bg` and on `surface` should reach 4.5:1 (WCAG AA);
  `textMuted`, `accent`, `warning` and `danger` on `bg` 3:1;
- with `--against-qml src/app/qml/Theme.qml`, that every role Theme.qml
  defines, the partition swatches and the free swatch come out identical from
  the bundled files, and that every Theme.qml palette has a file. Themes that
  `inherits` are not compared; typography is not compared.

`themes/qtheme.schema.json` is a JSON Schema (2020-12) for editors; it covers
structure and syntax but not reference resolution, inheritance, the SVG
rules or contrast.

## 11. What the format deliberately leaves out

- Per-widget colours. Everything is a role; a theme that needs a new role is a
  request to extend the role table, not a theme-local key.
- Icon drawings. Icons are strokes in role colours; `tint` on a background
  image is the only place a theme brings its own artwork.
- Layout metrics (spacing, radii, control heights, motion), see §9.
- Raster images, animation, per-view backgrounds, a directory or archive
  form. Version 2 material, if a real theme asks for it.

## 12. Loading, when it lands

The loader will live beside Settings. Bundled themes come from the resource
`:/themes/`; user themes from `themes/` under
`QStandardPaths::AppDataLocation`, and from the directory named by
`DRSTEIN_THEMES_DIR` when it is set, which is how a theme under development is
tried without installing it. `Settings.palette` keeps holding the theme `id`.
Theme.qml keeps its API; its tables become the resolved roles of the selected
theme, so no view changes, and the roles of §5 that views compute from ramp
steps today become Theme.qml properties fed by the loader.

The background needs one new item: the `ApplicationWindow`'s `color` becomes
the colour layer, and its `background` item gets a Shapes
`LinearGradient`/`RadialGradient` (the only Qt Quick gradients that take an
arbitrary angle) and one `Image` per image entry using the SVG module, with
the tint applied by substituting `currentColor` in the SVG text before the
image is loaded.
