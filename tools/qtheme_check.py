#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
qtheme_check: validate .qtheme files (doc/design/07-qtheme-format.md) and keep the
bundled ones in step with src/app/qml/Theme.qml.

  qtheme_check.py themes/*.qtheme themes/examples/*.qtheme
  qtheme_check.py --against-qml src/app/qml/Theme.qml themes/*.qtheme

Standard library only. The resolver below is the reference reading of the format's
rules (contrast steps, role defaults, references, inheritance); a loader in the app
must agree with it on every bundled theme.
"""
import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

FORMAT_VERSION = 1
IDENT = re.compile(r"^[a-z][a-z0-9-]*$")
HEX = re.compile(r"^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$")
RAMP_HEX = re.compile(r"^#[0-9a-fA-F]{6}$")
RAMP_REF = re.compile(r"^(accent|neutral)\.([1-9]00)$")
ALPHA = re.compile(r"^(.*)@((100|[1-9]?[0-9])%|0?\.[0-9]+|1(?:\.0+)?|0)$")
SRC = re.compile(r"^(?!/)(?!.*(^|/)\.\.(/|$))[^\\]+\.svg$")
ANCHOR = re.compile(r"^(center|top|bottom|left|right|(top|bottom) (left|right))$")

BASE_ROLES = ("bg", "surface", "text", "edge1", "edge2", "edge3")
TOP_ROLES = ("accent", "accent2")
DERIVED_ROLES = ("divider", "textBright", "textSoft", "textMuted", "textDim", "sunken", "hover", "selectedRow",
                 "selectedTint", "link", "selectionBg", "selectionText", "tagFill", "tagText", "scrim",
                 "ok", "warning", "danger", "info")
ROLES = BASE_ROLES + TOP_ROLES + DERIVED_ROLES
METADATA_KEYS = ("description", "author", "license", "version", "notes")
TOP_KEYS = {"qtheme", "id", "name", *METADATA_KEYS, "inherits", "vars", "accent", "accent2", "ramps", "schemes",
            "roles", "partitions", "background", "typography"}
SCHEME_KEYS = set(BASE_ROLES) | {"roles", "background"}
BG_KEYS = {"color", "gradient", "image"}
GRADIENT_KEYS = {"type", "angle", "center", "radius", "stops", "space"}
LINEAR_ONLY, RADIAL_ONLY = {"angle"}, {"center", "radius"}
IMAGE_KEYS = {"src", "fit", "anchor", "offset", "scale", "opacity", "tint", "blend", "space"}
FITS = {"cover", "contain", "stretch", "tile", "none"}
BLENDS = {"normal", "multiply", "screen"}
SPACES = {"window", "content"}
FONT_SIZES = ("body", "small", "tiny", "micro", "h1", "h2", "h3")
SVG_NS = "{http://www.w3.org/2000/svg}svg"
SVG_MAX_BYTES = 1 << 20

# Role defaults (§5): a reference string, or a function of the scheme name.
ROLE_DEFAULTS = {
    "divider": "text@16%",
    "textBright": "neutral.200",
    "textSoft": "neutral.400",
    "textMuted": "neutral.500",
    "textDim": "neutral.600",
    "sunken": "neutral.900",
    "hover": "surface",
    "selectedRow": "surface",
    "selectedTint": "accent.900",
    "link": "accent.300",
    "selectionBg": "accent.700",
    "selectionText": "accent.100",
    "tagFill": "neutral.800",
    "tagText": "neutral.100",
    "scrim": lambda s: "#000000@55%" if s == "dark" else "#000000@30%",
    "ok": lambda s: "accent.400" if s == "dark" else "accent.600",
    "warning": lambda s: "#e0b35a" if s == "dark" else "#9a6b00",
    "danger": lambda s: "#e37272" if s == "dark" else "#b23a3a",
    "info": "textSoft",
}
ACCENT2_DEFAULT = "accent.300"
PARTITION_DEFAULTS = {
    "swatches": [
        {"fill": "accent.800", "text": "accent.100"},
        {"fill": "accent.700", "text": "accent.100"},
        {"fill": "neutral.800", "text": "textBright"},
        {"fill": "neutral.700", "text": "textBright"},
        {"fill": "accent.600", "text": "accent.100"},
        {"fill": "neutral.600", "text": "textBright"},
    ],
    "free": {"fill": "sunken", "text": "textMuted"},
}


class ThemeError(Exception):
    pass


# ----------------------------------------------------------------------------- small helpers
def is_int(v):
    return (isinstance(v, int) and not isinstance(v, bool)) or (isinstance(v, float) and v.is_integer())


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def parse_hex(text):
    m = HEX.match(text)
    if not m:
        raise ThemeError(f"not a colour: {text!r}")
    h = m.group(1)
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 1.0)


def to_hex(c):
    r, g, b, a = c
    s = f"#{r:02x}{g:02x}{b:02x}"
    return s if a >= 0.999 else s + f"@{round(a * 100)}%"


def parse_alpha(text):
    v = float(text[:-1]) / 100 if text.endswith("%") else float(text)
    if not 0 <= v <= 1:
        raise ThemeError(f"alpha out of range: {text}")
    return v


def split_colour(text):
    """(base, alpha text or None); raises for a suffix on 'transparent'."""
    m = ALPHA.match(text)
    if not m:
        return text, None
    if m.group(1) == "transparent":
        raise ThemeError("'transparent' takes no alpha suffix")
    return m.group(1), m.group(2)


def valid_colour_syntax(text):
    """True when the string is a colour the format allows, without resolving it."""
    if not isinstance(text, str):
        return False
    try:
        base, _ = split_colour(text)
    except ThemeError:
        return False
    return bool(HEX.match(base) or base == "transparent" or RAMP_REF.match(base) or base in ROLES
                or (base.startswith("$") and IDENT.match(base[1:])))


# ----------------------------------------------------------------------------- loading and inheritance
def deep_merge(parent, child):
    """§8: objects merge key by key at every depth, arrays and scalars replace, null removes."""
    out = dict(parent)
    for k, v in child.items():
        if v is None:
            out.pop(k, None)
        elif k in out and isinstance(out[k], dict) and isinstance(v, dict):
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def image_entries(bg):
    """The image layer as a list (it may be one object, a list, or absent)."""
    im = (bg or {}).get("image")
    if im is None:
        return []
    return im if isinstance(im, list) else [im]


def resolve_asset_paths(raw, theme_dir):
    """§7.3: src is relative to the file that names it; turn it absolute and check it stays inside the theme dir."""
    errors = []
    blocks = [("background", raw.get("background"))]
    for s, body in (raw.get("schemes") or {}).items():
        if isinstance(body, dict):
            blocks.append((f"schemes.{s}.background", body.get("background")))
    for where, bg in blocks:
        for im in image_entries(bg if isinstance(bg, dict) else None):
            src = im.get("src")
            if not isinstance(src, str):
                continue
            target = (theme_dir / src).resolve()
            if not target.is_relative_to(theme_dir.resolve()):
                errors.append(f"{where}.image.src {src!r} leaves the theme's directory")
            im["src"] = str(target)
    return errors


def load_theme(path, search_dirs, seen=None):
    """Returns (merged theme, inherited: bool)."""
    seen = seen or []
    path = Path(path)
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise ThemeError(f"{path}: cannot read: {e}")
    if not isinstance(raw, dict):
        raise ThemeError(f"{path}: not a JSON object")
    errors = check_structure(raw, path)
    if errors:
        raise ThemeError("\n".join(errors))
    if raw["id"] != path.stem:
        raise ThemeError(f"{path}: id {raw['id']!r} does not match the file name {path.stem!r}")
    errors = [f"{path}: {m}" for m in resolve_asset_paths(raw, path.parent)]
    if errors:
        raise ThemeError("\n".join(errors))
    parent_id = raw.get("inherits")
    if parent_id is None:
        return deep_merge({}, raw), False
    if parent_id in seen + [raw["id"]]:
        raise ThemeError(f"{path}: inheritance cycle through {' -> '.join(seen + [raw['id'], parent_id])}")
    for d in list(search_dirs) + [path.parent]:
        candidate = Path(d) / f"{parent_id}.qtheme"
        if candidate.is_file():
            parent, _ = load_theme(candidate, search_dirs, seen + [raw["id"]])
            for k in METADATA_KEYS:
                parent.pop(k, None)
            merged = deep_merge(parent, raw)
            merged["id"], merged["name"] = raw["id"], raw["name"]
            merged.pop("inherits", None)
            return merged, True
    raise ThemeError(f"{path}: inherits {parent_id!r} but no {parent_id}.qtheme was found in "
                     f"{', '.join(str(d) for d in list(search_dirs) + [path.parent])}")


# ----------------------------------------------------------------------------- structure
def check_structure(t, where):
    e = []
    err = lambda m: e.append(f"{where}: {m}")
    v = t.get("qtheme")
    if isinstance(v, bool) or v != FORMAT_VERSION:
        err(f"qtheme must be {FORMAT_VERSION}, got {v!r}")
    for k in ("id", "name"):
        if not isinstance(t.get(k), str) or not t[k]:
            err(f"{k} is required and must be a non-empty string")
    if isinstance(t.get("id"), str) and not IDENT.match(t["id"]):
        err(f"id {t['id']!r} must match [a-z][a-z0-9-]*")
    if "inherits" in t and not (isinstance(t["inherits"], str) and IDENT.match(t["inherits"])):
        err("inherits must be an identifier")
    for k in t:
        if k not in TOP_KEYS:
            err(f"unknown key {k!r}")
    for k in METADATA_KEYS:
        if k in t and t[k] is not None and not isinstance(t[k], str):
            err(f"{k} must be a string")
    if t.get("vars") is not None:
        if not isinstance(t["vars"], dict):
            err("vars must be an object")
        else:
            for k, v in t["vars"].items():
                if not IDENT.match(k):
                    err(f"vars: bad name {k!r}")
                if not valid_colour_syntax(v):
                    err(f"vars.{k}: not a colour: {v!r}")
    for k in TOP_ROLES:
        if k in t and (t[k] is not None or k == "accent"):
            v = t[k]
            if isinstance(v, dict):
                if set(v) != {"dark", "light"} or not all(valid_colour_syntax(x) for x in v.values()):
                    err(f"{k}: a colour or {{dark, light}} colours")
            elif not valid_colour_syntax(v):
                err(f"{k}: not a colour: {v!r}")
    if t.get("ramps") is not None:
        if not isinstance(t["ramps"], dict):
            err("ramps must be an object")
        else:
            for k, v in t["ramps"].items():
                if k not in ("accent", "neutral"):
                    err(f"ramps: unknown ramp {k!r}")
                elif v is not None and not (isinstance(v, list) and len(v) == 9 and all(isinstance(c, str) and RAMP_HEX.match(c) for c in v)):
                    err(f"ramps.{k}: nine #rrggbb literals, lightest first")
    if "schemes" in t:
        if not isinstance(t["schemes"], dict):
            err("schemes must be an object")
        else:
            for s, body in t["schemes"].items():
                if s not in ("dark", "light"):
                    err(f"schemes: unknown scheme {s!r}")
                    continue
                if not isinstance(body, dict):
                    err(f"schemes.{s} must be an object")
                    continue
                for k, v in body.items():
                    if k not in SCHEME_KEYS:
                        err(f"schemes.{s}: unknown key {k!r}")
                    elif k in BASE_ROLES and not valid_colour_syntax(v):
                        err(f"schemes.{s}.{k}: not a colour: {v!r}")
                e += check_roles(body.get("roles"), f"{where}: schemes.{s}.roles")
                e += check_background(body.get("background"), f"{where}: schemes.{s}.background")
    e += check_roles(t.get("roles"), f"{where}: roles")
    e += check_partitions(t.get("partitions"), f"{where}: partitions")
    e += check_background(t.get("background"), f"{where}: background")
    e += check_typography(t.get("typography"), f"{where}: typography")
    return e


def check_roles(r, where):
    if r is None:
        return []
    if not isinstance(r, dict):
        return [f"{where}: must be an object"]
    e = []
    for k, v in r.items():
        if k in BASE_ROLES:
            e.append(f"{where}: {k!r} is a base colour; set it in the scheme itself")
        elif k in TOP_ROLES:
            e.append(f"{where}: {k!r} is set at the top level, not in roles")
        elif k not in DERIVED_ROLES:
            e.append(f"{where}: unknown role {k!r}")
        elif v is not None and not valid_colour_syntax(v):
            e.append(f"{where}.{k}: not a colour: {v!r}")
    return e


def check_swatch(sw, where):
    if not (isinstance(sw, dict) and set(sw) == {"fill", "text"}):
        return [f"{where}: {{fill, text}}"]
    return [f"{where}.{k}: not a colour: {sw[k]!r}" for k in ("fill", "text") if not valid_colour_syntax(sw[k])]


def check_partitions(p, where):
    if p is None:
        return []
    if not isinstance(p, dict):
        return [f"{where}: must be an object"]
    e = []
    for k, v in p.items():
        if k == "swatches":
            if not (isinstance(v, list) and v):
                e.append(f"{where}.swatches: a non-empty list of {{fill, text}} swatches")
            else:
                for i, sw in enumerate(v):
                    e += check_swatch(sw, f"{where}.swatches[{i}]")
        elif k == "free":
            if v is not None:
                e += check_swatch(v, f"{where}.free")
        else:
            e.append(f"{where}: unknown key {k!r}")
    return e


def check_gradient(g, where):
    if not isinstance(g, dict):
        return [f"{where}: must be an object"]
    e = []
    for k in g:
        if k not in GRADIENT_KEYS:
            e.append(f"{where}: unknown key {k!r}")
    kind = g.get("type")
    if kind is None:
        kind = "linear"
    if kind not in ("linear", "radial"):
        e.append(f"{where}.type: linear or radial")
        kind = "linear"
    for k in (RADIAL_ONLY if kind == "linear" else LINEAR_ONLY):
        if k in g and g[k] is not None:
            e.append(f"{where}.{k}: not valid for a {kind} gradient")
    if g.get("angle") is not None and not is_num(g["angle"]):
        e.append(f"{where}.angle: a number of degrees")
    if g.get("center") is not None and not (isinstance(g["center"], list) and len(g["center"]) == 2 and all(is_num(x) for x in g["center"])):
        e.append(f"{where}.center: [x, y] fractions")
    if g.get("radius") is not None and not (is_num(g["radius"]) and g["radius"] > 0):
        e.append(f"{where}.radius: a positive fraction")
    if g.get("space") is not None and g["space"] not in SPACES:
        e.append(f"{where}.space: window or content")
    stops = g.get("stops")
    if not (isinstance(stops, list) and len(stops) >= 2):
        e.append(f"{where}.stops: at least two stops")
    else:
        last = -1.0
        for i, s in enumerate(stops):
            if not (isinstance(s, dict) and set(s) == {"at", "color"}):
                e.append(f"{where}.stops[{i}]: {{at, color}}")
                continue
            at = s["at"]
            if not (is_num(at) and 0 <= at <= 1):
                e.append(f"{where}.stops[{i}].at: 0..1")
            elif at < last:
                e.append(f"{where}.stops[{i}].at: stops must not decrease")
            else:
                last = at
            if not valid_colour_syntax(s["color"]):
                e.append(f"{where}.stops[{i}].color: not a colour")
    return e


def check_image(im, where):
    if not isinstance(im, dict):
        return [f"{where}: must be an object"]
    e = []
    for k in im:
        if k not in IMAGE_KEYS:
            e.append(f"{where}: unknown key {k!r}")
    src = im.get("src")
    if not (isinstance(src, str) and SRC.match(src)):
        e.append(f"{where}.src: a relative path with forward slashes, no '..', ending in .svg")
    if im.get("fit") is not None and im["fit"] not in FITS:
        e.append(f"{where}.fit: one of {sorted(FITS)}")
    if im.get("anchor") is not None and not (isinstance(im["anchor"], str) and ANCHOR.match(im["anchor"])):
        e.append(f"{where}.anchor: center, top, bottom, left, right, or 'top left'-style vertical-then-horizontal pairs")
    if im.get("offset") is not None and not (isinstance(im["offset"], list) and len(im["offset"]) == 2 and all(is_num(x) for x in im["offset"])):
        e.append(f"{where}.offset: [x, y] pixels")
    if im.get("scale") is not None and not (is_num(im["scale"]) and im["scale"] > 0):
        e.append(f"{where}.scale: positive")
    if im.get("opacity") is not None and not (is_num(im["opacity"]) and 0 <= im["opacity"] <= 1):
        e.append(f"{where}.opacity: 0..1")
    if im.get("tint") is not None and not valid_colour_syntax(im["tint"]):
        e.append(f"{where}.tint: not a colour")
    if im.get("blend") is not None and im["blend"] not in BLENDS:
        e.append(f"{where}.blend: one of {sorted(BLENDS)}")
    if im.get("space") is not None and im["space"] not in SPACES:
        e.append(f"{where}.space: window or content")
    return e


def check_background(b, where):
    if b is None:
        return []
    if not isinstance(b, dict):
        return [f"{where}: must be an object"]
    e = []
    for k in b:
        if k not in BG_KEYS:
            e.append(f"{where}: unknown key {k!r}")
    if b.get("color") is not None and not valid_colour_syntax(b["color"]):
        e.append(f"{where}.color: not a colour")
    if b.get("gradient") is not None:
        e += check_gradient(b["gradient"], f"{where}.gradient")
    im = b.get("image")
    if im is not None:
        if isinstance(im, list):
            if not im:
                e.append(f"{where}.image: an image or a non-empty list of images")
            for i, x in enumerate(im):
                e += check_image(x, f"{where}.image[{i}]")
        else:
            e += check_image(im, f"{where}.image")
    return e


def check_typography(t, where):
    if t is None:
        return []
    if not isinstance(t, dict):
        return [f"{where}: must be an object"]
    e = []
    for k, v in t.items():
        if v is None:
            continue
        if k in ("family", "mono"):
            if not (isinstance(v, list) and v and all(isinstance(x, str) and x for x in v)):
                e.append(f"{where}.{k}: a non-empty list of family names, preferred first")
        elif k == "sizes":
            if not isinstance(v, dict):
                e.append(f"{where}.sizes: must be an object")
            else:
                for n, px in v.items():
                    if n not in FONT_SIZES:
                        e.append(f"{where}.sizes.{n}: unknown size (one of {', '.join(FONT_SIZES)})")
                    elif px is not None and not (is_int(px) and 1 <= px <= 512):
                        e.append(f"{where}.sizes.{n}: a pixel count from 1 to 512")
        elif k == "headingWeight":
            if not (is_int(v) and 100 <= v <= 900 and v % 100 == 0):
                e.append(f"{where}.headingWeight: 100..900 in steps of 100")
        else:
            e.append(f"{where}: unknown key {k!r}")
    return e


# ----------------------------------------------------------------------------- SVG
BAD_SVG_ELEMENTS = {"script", "image", "foreignObject"}
URL_REF = re.compile(r"url\(\s*['\"]?([^'\")]*)")


def check_svg(svg_path):
    """§7.3: self-contained SVG. Returns a list of problems (empty when fine)."""
    problems = []
    try:
        data = svg_path.read_bytes()
    except OSError as e:
        return [f"cannot read: {e}"]
    if len(data) > SVG_MAX_BYTES:
        problems.append(f"is {len(data)} bytes, above 1 MiB")
    if re.search(rb"<!DOCTYPE", data, re.I):
        return problems + ["has a DOCTYPE"]
    try:
        root = ET.fromstring(data)
    except ET.ParseError as e:
        return problems + [f"is not well-formed XML: {e}"]
    if root.tag != SVG_NS:
        problems.append("root element is not <svg> in the SVG namespace")
    elif "viewBox" not in root.attrib:
        problems.append("<svg> has no viewBox")

    def check_urls(text, what):
        for m in URL_REF.finditer(text):
            if not m.group(1).strip().startswith("#"):
                problems.append(f"{what} url({m.group(1)!r}) points outside the file")
        if "@import" in text:
            problems.append(f"{what} uses @import")

    for el in root.iter():
        local = el.tag.rsplit("}", 1)[-1]
        if local in BAD_SVG_ELEMENTS:
            problems.append(f"contains <{local}>")
        for k, v in el.attrib.items():
            name = k.rsplit("}", 1)[-1]
            if name.lower().startswith("on"):
                problems.append(f"has an event attribute {name}")
            elif name == "href" and not v.startswith("#"):
                problems.append(f"href {v!r} points outside the file")
            elif name == "style":
                check_urls(v, "style attribute")
            elif "url(" in v:
                check_urls(v, f"attribute {name}")
        if local == "style" and el.text:
            check_urls(el.text, "<style>")
    return problems


# ----------------------------------------------------------------------------- resolving
class Resolver:
    """Turns a merged theme into concrete colours for one scheme, by the rules of §3, §4 and §5."""

    def __init__(self, theme, scheme):
        self.t, self.s = theme, scheme
        self.vars = theme.get("vars") or {}
        self.cache = {}
        self.stack = []

    def ramp(self, name, step):
        ramp = (self.t.get("ramps") or {}).get(name)
        if not ramp:
            raise ThemeError(f"ramps.{name} is missing")
        i = step // 100 - 1
        return parse_hex(ramp[i] if self.s == "dark" else ramp[8 - i])

    def role_source(self, role):
        scheme = (self.t.get("schemes") or {}).get(self.s) or {}
        if role in BASE_ROLES:
            if role not in scheme:
                raise ThemeError(f"schemes.{self.s}.{role} is missing")
            return scheme[role]
        if role in TOP_ROLES:
            a = self.t.get(role)
            if a is None:
                if role == "accent":
                    raise ThemeError("accent is missing")
                return ACCENT2_DEFAULT
            return a[self.s] if isinstance(a, dict) else a
        if role in (scheme.get("roles") or {}):
            return scheme["roles"][role]
        if role in (self.t.get("roles") or {}):
            return self.t["roles"][role]
        d = ROLE_DEFAULTS[role]
        return d(self.s) if callable(d) else d

    def colour(self, text, context="value"):
        if not isinstance(text, str):
            raise ThemeError(f"{context}: not a colour: {text!r}")
        try:
            base, alpha_text = split_colour(text)
        except ThemeError as e:
            raise ThemeError(f"{context}: {e}")
        alpha = parse_alpha(alpha_text) if alpha_text is not None else None
        if HEX.match(base):
            c = parse_hex(base)
        elif base == "transparent":
            c = (0, 0, 0, 0.0)
        elif RAMP_REF.match(base):
            name, step = RAMP_REF.match(base).groups()
            c = self.ramp(name, int(step))
        elif base.startswith("$"):
            name = base[1:]
            if name not in self.vars:
                raise ThemeError(f"{context}: unknown variable {base}")
            c = self.guarded(base, lambda: self.colour(self.vars[name], f"vars.{name}"))
        elif base in ROLES:
            c = self.role(base)
        else:
            raise ThemeError(f"{context}: not a colour: {text!r}")
        if alpha is not None:
            c = (c[0], c[1], c[2], alpha)
        return c

    def guarded(self, key, compute):
        if key in self.stack:
            raise ThemeError(f"reference cycle: {' -> '.join(self.stack + [key])}")
        self.stack.append(key)
        try:
            return compute()
        finally:
            self.stack.pop()

    def role(self, name):
        if name in self.cache:
            return self.cache[name]
        c = self.guarded(name, lambda: self.colour(self.role_source(name), f"role {name}"))
        self.cache[name] = c
        return c

    def all_roles(self):
        return {r: self.role(r) for r in ROLES}


# ----------------------------------------------------------------------------- contrast
def luminance(c):
    def lin(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])


def over(fg, bg):
    a = fg[3]
    return tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3)) + (1.0,)


def contrast(fg, bg):
    fg = over(fg, bg)
    l1, l2 = luminance(fg), luminance(bg)
    hi, lo = max(l1, l2), min(l1, l2)
    return (hi + 0.05) / (lo + 0.05)


CONTRAST_ADVICE = [("text", "bg", 4.5), ("text", "surface", 4.5), ("textMuted", "bg", 3.0), ("accent", "bg", 3.0),
                   ("warning", "bg", 3.0), ("danger", "bg", 3.0)]


# ----------------------------------------------------------------------------- Theme.qml comparison
def palettes_from_qml(path):
    src = Path(path).read_text(encoding="utf-8")
    start = src.index("readonly property var palettes: (") + len("readonly property var palettes: (")
    block = src[start:]
    block = block[: block.index("\n    })") + 1] + "}"
    block = re.sub(r"//[^\n]*", "", block)
    block = re.sub(r"(\b[a-zA-Z_][a-zA-Z0-9_]*)\s*:", r'"\1":', block)
    block = re.sub(r",(\s*[}\]])", r"\1", block)
    return json.loads(block)


def compare_with_qml(theme, qml):
    """Every colour Theme.qml derives today must come out identical from the theme file."""
    p = qml.get(theme["id"])
    if p is None:
        return [f"Theme.qml has no palette {theme['id']!r}"]
    e = []
    same = lambda a, b: tuple(a[:3]) == tuple(b[:3]) and abs(a[3] - b[3]) <= 0.01
    for s in ("dark", "light"):
        r = Resolver(theme, s)
        ramp_a = p["accentRamp"] if s == "dark" else list(reversed(p["accentRamp"]))
        ramp_n = p["neutralRamp"] if s == "dark" else list(reversed(p["neutralRamp"]))
        step = lambda ramp, n: parse_hex(ramp[n // 100 - 1])
        g = p[s]
        text = parse_hex(g["text"])
        expected = {  # Theme.qml's own properties; roles it does not define yet are not compared.
            "bg": parse_hex(g["bg"]), "surface": parse_hex(g["surface"]), "text": text,
            "edge1": parse_hex(g["edge"]), "edge2": parse_hex(g["edge2"]), "edge3": parse_hex(g["edge3"]),
            "accent": parse_hex(p["accent"] if s == "dark" else p["accentRamp"][5]),
            "accent2": parse_hex(p["accent2"]),
            "divider": (text[0], text[1], text[2], 0.16),
            "textMuted": step(ramp_n, 500), "textDim": step(ramp_n, 600), "textSoft": step(ramp_n, 400), "textBright": step(ramp_n, 200),
            "sunken": step(ramp_n, 900), "hover": parse_hex(g["surface"]), "selectedRow": parse_hex(g["surface"]), "selectedTint": step(ramp_a, 900),
            "ok": step(ramp_a, 400 if s == "dark" else 600),
            "warning": parse_hex("#e0b35a" if s == "dark" else "#9a6b00"),
            "danger": parse_hex("#e37272" if s == "dark" else "#b23a3a"),
            "info": step(ramp_n, 400),
        }
        got = r.all_roles()
        for role, want in expected.items():
            if not same(got[role], want):
                e.append(f"{s}.{role}: theme gives {to_hex(got[role])}, Theme.qml gives {to_hex(want)}")
        # Partition swatches: partitionPalette, partitionTextColor and partitionColor(-1).
        part = theme.get("partitions") or {}
        swatches = part.get("swatches", PARTITION_DEFAULTS["swatches"])
        free = part.get("free", PARTITION_DEFAULTS["free"])
        want_fill = [step(ramp_a, 800), step(ramp_a, 700), step(ramp_n, 800), step(ramp_n, 700), step(ramp_a, 600), step(ramp_n, 600)]
        want_text = [step(ramp_a, 100), step(ramp_a, 100), step(ramp_n, 200), step(ramp_n, 200), step(ramp_a, 100), step(ramp_n, 200)]
        have_fill = [r.colour(sw["fill"], "partitions.swatches") for sw in swatches]
        have_text = [r.colour(sw["text"], "partitions.swatches") for sw in swatches]
        if len(have_fill) != len(want_fill) or not all(same(h, w) for h, w in zip(have_fill, want_fill)):
            e.append(f"{s}: partition fills differ from Theme.qml")
        if len(have_text) != len(want_text) or not all(same(h, w) for h, w in zip(have_text, want_text)):
            e.append(f"{s}: partition text colours differ from Theme.qml")
        if not same(r.colour(free["fill"], "partitions.free"), step(ramp_n, 900)):
            e.append(f"{s}: free-space fill differs from Theme.qml")
        if not same(r.colour(free["text"], "partitions.free"), step(ramp_n, 500)):
            e.append(f"{s}: free-space text differs from Theme.qml")
    return e


# ----------------------------------------------------------------------------- main
def check_file(path, search_dirs, qml, strict_contrast):
    """Returns (errors, warnings, theme id or None)."""
    errors, warnings = [], []
    try:
        theme, inherited = load_theme(path, search_dirs)
    except ThemeError as e:
        return [str(e)], [], None
    for s in ("dark", "light"):
        if s not in (theme.get("schemes") or {}):
            errors.append(f"{path}: schemes.{s} is missing (after inheritance)")
            continue
        try:
            r = Resolver(theme, s)
            roles = r.all_roles()
            for n in theme.get("vars") or {}:
                r.colour(f"${n}", f"vars.{n}")
            part = theme.get("partitions") or {}
            for i, sw in enumerate(part.get("swatches", PARTITION_DEFAULTS["swatches"])):
                for k in ("fill", "text"):
                    r.colour(sw[k], f"partitions.swatches[{i}].{k}")
            for k in ("fill", "text"):
                r.colour(part.get("free", PARTITION_DEFAULTS["free"])[k], f"partitions.free.{k}")
            bg = deep_merge(theme.get("background") or {}, (theme["schemes"][s].get("background") or {}))
            if bg:
                colour = r.colour(bg.get("color", "bg"), f"schemes.{s}.background.color")
                if colour[3] < 0.999:
                    errors.append(f"{path}: {s}: background.color must be opaque, got {to_hex(colour)}")
                for i, st in enumerate((bg.get("gradient") or {}).get("stops") or []):
                    r.colour(st["color"], f"schemes.{s}.background.gradient.stops[{i}]")
                for i, im in enumerate(image_entries(bg)):
                    if "tint" in im:
                        r.colour(im["tint"], f"schemes.{s}.background.image[{i}].tint")
                    svg = Path(im["src"])
                    if not svg.is_file():
                        errors.append(f"{path}: {s}: background.image[{i}].src {svg.name!r} not found")
                    else:
                        for problem in check_svg(svg):
                            errors.append(f"{path}: {s}: background.image[{i}] {svg.name}: {problem}")
            for fg, on, minimum in CONTRAST_ADVICE:
                ratio = contrast(roles[fg], roles[on])
                if ratio < minimum:
                    msg = f"{path}: {s}: {fg} on {on} is {ratio:.2f}:1, below {minimum}:1"
                    (errors if strict_contrast else warnings).append(msg)
        except ThemeError as e:
            errors.append(f"{path}: {s}: {e}")
    if qml is not None and not inherited and not errors:
        errors += [f"{path}: {m}" for m in compare_with_qml(theme, qml)]
    return errors, warnings, theme.get("id")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("themes", nargs="+", type=Path, help=".qtheme files or directories")
    ap.add_argument("--themes-dir", action="append", default=[], type=Path, help="extra directories to resolve 'inherits' in")
    ap.add_argument("--against-qml", type=Path, help="Theme.qml to compare the bundled (non-inheriting) themes with")
    ap.add_argument("--strict-contrast", action="store_true", help="treat contrast advice as errors")
    a = ap.parse_args(argv)
    files = []
    for p in a.themes:
        files += sorted(p.glob("*.qtheme")) if p.is_dir() else [p]
    search = [Path(__file__).resolve().parent.parent / "themes"] + a.themes_dir
    qml = palettes_from_qml(a.against_qml) if a.against_qml else None
    bad, ids = 0, set()
    for f in files:
        try:
            errors, warnings, theme_id = check_file(f, search, qml, a.strict_contrast)
        except Exception as e:  # a bug in this checker must not hide the other files' results
            errors, warnings, theme_id = [f"{f}: checker failure: {type(e).__name__}: {e}"], [], None
        for w in warnings:
            print(f"warning: {w}")
        for e in errors:
            print(f"error: {e}")
        if errors:
            bad += 1
        else:
            print(f"ok: {f}")
        if theme_id:
            ids.add(theme_id)
    if qml is not None:
        for pid in qml:
            if pid not in ids:
                print(f"error: Theme.qml palette {pid!r} has no .qtheme among the checked files")
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
