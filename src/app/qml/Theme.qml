// SPDX-License-Identifier: MIT
// The design tokens, translated from the Nocturne design system the mockup
// uses (doc/design/02-ui-translation.md). Two palettes × two schemes; the
// numbers are the mockup's own.
pragma Singleton
import QtQuick
import DrStein

QtObject {
    id: theme

    readonly property bool dark: Settings.colorScheme === "dark"
                                 || (Settings.colorScheme === "system" && Application.styleHints.colorScheme === Qt.Dark)

    readonly property var palettes: ({
        blurple: {
            accent: "#9184d9", accent2: "#a7a1db",
            accentRamp: ["#f5f4ff", "#e7e5fe", "#d2cefd", "#b5abfc", "#968ae0", "#796cbf", "#5d5294", "#423a6a", "#2b2741"],
            neutralRamp: ["#f3f5fe", "#e4e7f5", "#cfd3e5", "#b2b6ca", "#9397ab", "#75798c", "#595d6c", "#3f424d", "#292b31"],
            dark: { bg: "#161826", surface: "#232532", text: "#e9e9ed", edge: "#3f424d", edge2: "#595d6c", edge3: "#9397ab" },
            light: { bg: "#eef0f7", surface: "#f9f9fd", text: "#1c1e2b", edge: "#cfd3e5", edge2: "#b2b6ca", edge3: "#75798c" }
        },
        // Apple's system blue and greys, for people who want the app to sit next to Finder.
        blue: {
            accent: "#0a84ff", accent2: "#64b5ff",
            accentRamp: ["#eaf4ff", "#cfe6ff", "#a8d1ff", "#7ab8ff", "#4da3ff", "#0a84ff", "#0066d6", "#004ea8", "#003a7d"],
            neutralRamp: ["#f5f5f7", "#e5e5ea", "#d1d1d6", "#c7c7cc", "#aeaeb2", "#8e8e93", "#636366", "#48484a", "#2c2c2e"],
            dark: { bg: "#1c1c1e", surface: "#2c2c2e", text: "#f2f2f7", edge: "#3a3a3c", edge2: "#48484a", edge3: "#8e8e93" },
            light: { bg: "#f2f2f7", surface: "#ffffff", text: "#1c1c1e", edge: "#d1d1d6", edge2: "#c7c7cc", edge3: "#8e8e93" }
        },
        // No hue at all: the accent is a lighter grey, status colours are the only colour left.
        grey: {
            accent: "#a8a8a8", accent2: "#c2c2c2",
            accentRamp: ["#f6f6f6", "#e8e8e8", "#d4d4d4", "#bcbcbc", "#a8a8a8", "#8a8a8a", "#6b6b6b", "#4c4c4c", "#323232"],
            neutralRamp: ["#f0f0f0", "#e0e0e0", "#c8c8c8", "#adadad", "#8f8f8f", "#727272", "#565656", "#3d3d3d", "#282828"],
            dark: { bg: "#141414", surface: "#1f1f1f", text: "#e8e8e8", edge: "#2e2e2e", edge2: "#454545", edge3: "#7d7d7d" },
            light: { bg: "#ededed", surface: "#f9f9f9", text: "#1a1a1a", edge: "#cfcfcf", edge2: "#b5b5b5", edge3: "#727272" }
        },
        teal: {
            accent: "#3fc9c4", accent2: "#7ad3cf",
            accentRamp: ["#ecfffd", "#cdfaf7", "#a6f0ec", "#6fdfda", "#3fc9c4", "#22a39f", "#167c7a", "#105857", "#0b3a3a"],
            neutralRamp: ["#eef7f8", "#dceaec", "#c3d6d9", "#a4b9bd", "#869a9f", "#687c81", "#4e5f63", "#374448", "#23303a"],
            dark: { bg: "#0c1619", surface: "#132226", text: "#dff1f2", edge: "#24383d", edge2: "#375056", edge3: "#5f8389" },
            light: { bg: "#e9f3f4", surface: "#f6fbfb", text: "#10272a", edge: "#c3d6d9", edge2: "#a4b9bd", edge3: "#687c81" }
        }
    })

    readonly property var p: palettes[Settings.palette] !== undefined ? palettes[Settings.palette] : palettes.teal
    readonly property var g: dark ? p.dark : p.light
    readonly property var accentRamp: dark ? p.accentRamp : p.accentRamp.slice().reverse()
    readonly property var neutralRamp: dark ? p.neutralRamp : p.neutralRamp.slice().reverse()

    // Roles.
    readonly property color bg: g.bg
    readonly property color surface: g.surface
    readonly property color text: g.text
    readonly property color accent: dark ? p.accent : p.accentRamp[5]
    readonly property color accent2: p.accent2
    readonly property color divider: Qt.rgba(text.r, text.g, text.b, 0.16)
    readonly property color edge1: g.edge
    readonly property color edge2: g.edge2
    readonly property color edge3: g.edge3

    // Ramps: accent(100..900), neutral(100..900).
    function accentStep(step) { return accentRamp[Math.max(0, Math.min(8, step / 100 - 1))] }
    function neutralStep(step) { return neutralRamp[Math.max(0, Math.min(8, step / 100 - 1))] }
    readonly property color textMuted: neutralStep(500)
    readonly property color textDim: neutralStep(600)
    readonly property color textSoft: neutralStep(400)
    readonly property color textBright: neutralStep(200)
    readonly property color sunken: neutralStep(900)      // inset panels (rows, code blocks)
    readonly property color hover: surface
    readonly property color selectedRow: surface
    readonly property color selectedTint: accentStep(900)

    // Semantic (an addition to the mono design system, see the translation doc).
    readonly property color ok: dark ? accentStep(400) : accentStep(600)
    readonly property color warning: dark ? "#e0b35a" : "#9a6b00"
    readonly property color danger: dark ? "#e37272" : "#b23a3a"
    function healthColor(level) {
        if (level === "error") return danger
        if (level === "warning") return warning
        if (level === "info") return textSoft
        return ok
    }

    // Partition swatches (the mockup's sw[] plus two more, cycled).
    readonly property var partitionPalette: [accentStep(800), accentStep(700), neutralStep(800), neutralStep(700), accentStep(600), neutralStep(600)]
    function partitionColor(i) {
        if (i < 0) return sunken
        return partitionPalette[i % partitionPalette.length]
    }
    function partitionTextColor(i) { return i < 0 ? textMuted : (i % partitionPalette.length < 2 || i % partitionPalette.length === 4 ? accentStep(100) : textBright) }

    // Type.
    readonly property string fontFamily: Qt.fontFamilies().indexOf("Inter") >= 0 ? "Inter" : Application.font.family
    readonly property string monoFamily: Qt.platform.os === "osx" ? "Menlo" : (Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono")
    readonly property int fontSize: 13
    readonly property int fontSmall: 12
    readonly property int fontTiny: 11
    readonly property int fontMicro: 10
    readonly property int fontH1: 18
    readonly property int fontH2: 16
    readonly property int fontH3: 15
    readonly property int headingWeight: Font.Medium

    // Spacing (0.7× density) and radii.
    readonly property int space1: 3
    readonly property int space2: 6
    readonly property int space3: 8
    readonly property int space4: 11
    readonly property int space6: 17
    readonly property int space8: 22
    readonly property int radiusSm: 4
    readonly property int radiusMd: 8
    readonly property int radiusLg: 14
    readonly property int controlHeight: 28
    readonly property int controlHeightSmall: 24
    readonly property real disabledOpacity: 0.45

    // Animation.
    readonly property int quick: 120
}
