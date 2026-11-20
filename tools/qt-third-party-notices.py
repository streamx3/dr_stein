#!/usr/bin/env python3
"""Lists the third-party code inside the Qt that a Dr Stein install carries, with each piece's
copyright notices and licence text.

    qt-third-party-notices.py --docs <Qt>/Docs/Qt-6.12.0 --tree <dir with lib/ and plugins/>
                              --qt-version 6.12.0 --output qt-third-party.txt

Qt documents each piece of third-party code it contains on an attribution page of the module
that uses it (<docs>/qtgui/qtgui-attribution-libpng.html and so on): what it is, where Qt uses
it, its copyright lines and the full text of its licence. This copies, as plain text, the pages
of every Qt module whose library is in <tree>/lib (and of qtimageformats and qtsvg for the image
format and icon plugins), leaving out the pages about assets of Qt's examples. Run by the Linux
install (cmake/DrSteinInstallSteps.cmake). Standard library only.
"""
import argparse
import html
import pathlib
import re
import sys
import textwrap

PRE = re.compile(r"<pre\b[^>]*>(.*?)</pre>", re.S)
TAG = re.compile(r"<[^>]+>")
LINK = re.compile(r'<a\s[^>]*href="(https?://[^"]+)"[^>]*>(.*?)</a>', re.S)
PARAGRAPH = re.compile(r"<(p|li|h[1-6])\b[^>]*>(.*?)</\1>", re.S)


def module_doc_dirs(docs: pathlib.Path, tree: pathlib.Path) -> list[str]:
    """The documentation folders (qtcore, qtgui, ...) of the Qt modules in the tree."""
    available = sorted(d.name for d in docs.iterdir() if d.is_dir() and d.name.startswith("qt") and len(d.name) > 2)
    wanted = set()
    for lib in (tree / "lib").glob("libQt6*.so*"):
        match = re.match(r"libQt6(\w+?)\.so", lib.name)
        if not match:
            continue
        module = match.group(1).lower()
        # The longest folder name the module name starts with: quickcontrols2basic -> qtquickcontrols.
        candidates = [d for d in available if module.startswith(d[2:])]
        if candidates:
            wanted.add(max(candidates, key=len))
    plugins = tree / "plugins"
    if (plugins / "imageformats").is_dir() and "qtimageformats" in available:
        wanted.add("qtimageformats")
    if (plugins / "iconengines").is_dir() and "qtsvg" in available:
        wanted.add("qtsvg")
    return sorted(wanted)


def text_of(fragment: str) -> str:
    fragment = LINK.sub(lambda m: f"{m.group(2)} ({m.group(1)})" if TAG.sub("", m.group(2)).strip() != m.group(1) else m.group(1), fragment)
    return html.unescape(TAG.sub("", fragment))


def page_text(page: pathlib.Path) -> tuple[str, str] | None:
    """(title, body) of an attribution page, or None for the asset of an example."""
    source = page.read_text(encoding="utf-8")
    title = re.search(r'<h1 class="title"[^>]*>(.*?)</h1>', source, re.S)
    body = re.search(r'<div class="descr" id="details">(.*?)<!-- @@@', source, re.S)
    if not title or not body:
        raise SystemExit(f"{page}: not a Qt attribution page as expected (no title or description)")
    blocks = []
    pos = 0
    content = body.group(1)
    # Paragraphs are reflowed; licence texts (<pre>) are kept exactly as they are.
    for pre in PRE.finditer(content):
        blocks += [("p", text_of(m.group(2))) for m in PARAGRAPH.finditer(content, pos, pre.start())]
        blocks.append(("pre", text_of(pre.group(1))))
        pos = pre.end()
    blocks += [("p", text_of(m.group(2))) for m in PARAGRAPH.finditer(content, pos)]
    used_in = next((text for kind, text in blocks if kind == "p" and text.startswith("Used in")), "")
    if re.search(r"\bexample\b", used_in, re.I):
        return None
    out = []
    for kind, text in blocks:
        if kind == "pre":
            out.append("\n".join(line.rstrip() for line in text.strip("\n").splitlines()))
        else:
            text = " ".join(text.split())
            if text:
                out.append(textwrap.fill(text, 78))
    return " ".join(text_of(title.group(1)).split()), "\n\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--docs", required=True, type=pathlib.Path, help="Qt's documentation, <Qt>/Docs/Qt-<version>")
    parser.add_argument("--tree", required=True, type=pathlib.Path, help="the deployed tree, with lib/ and plugins/")
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    dirs = module_doc_dirs(args.docs, args.tree)
    if "qtcore" not in dirs:
        print(f"qt-third-party-notices.py: no libQt6Core in {args.tree}/lib or no qtcore in {args.docs}", file=sys.stderr)
        return 1
    sections = []
    for name in dirs:
        for page in sorted((args.docs / name).glob(f"{name}-attribution-*.html")):
            entry = page_text(page)
            if entry:
                sections.append((name, *entry))

    rule = "-" * 78
    header = textwrap.fill(
        f"The Qt {args.qt_version} libraries, plugins and QML modules installed with Dr Stein contain "
        "the third-party code listed here, under licences of its own. Each entry is the attribution "
        f"page of Qt {args.qt_version}'s documentation for the module that uses it (in brackets): what "
        "the code is, where Qt uses it, its copyright notices and its licence. Some describe code "
        "for other platforms or features, which the module's documentation lists as well.", 78)
    with args.output.open("w", encoding="utf-8") as out:
        out.write(f"Third-party code in Qt {args.qt_version}\n{'=' * 78}\n\n{header}\n")
        for name, title, body in sections:
            out.write(f"\n\n{rule}\n{title} ({name})\n{rule}\n\n{body}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
