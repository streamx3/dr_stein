# Codicons (window caption buttons on Linux)

The four caption-button glyphs Dr Stein draws on Linux are Microsoft's
Codicons, the icon set Visual Studio Code uses for its own title bar:
`chrome-minimize`, `chrome-maximize`, `chrome-restore`, `chrome-close`.

- Source: https://github.com/microsoft/vscode-codicons, `src/icons/`,
  commit 6b53088f5c55bce7107fb579765364f59cd4ad5d (2026-10-06).
- Files are unmodified. They paint with `currentColor`; `CodiconProvider`
  (`src/app/platform/codicon_provider.cpp`) substitutes the theme colour at
  load time and serves them as `image://codicon/<name>/<aarrggbb>`.
- License: Creative Commons Attribution 4.0 International (`LICENSE` here),
  © Microsoft Corporation. The attribution is this file and the README's
  licence paragraph.
