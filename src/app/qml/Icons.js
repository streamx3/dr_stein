.pragma library
// SPDX-License-Identifier: MIT
// Stroke icons on a 24-unit grid, in the mockup's style (1.6 stroke, round
// caps). The mockup's own paths come first; the rest follow Phosphor's shapes.
// (.pragma library sits on line 1: Qt's CMake only looks there for it.)

var paths = {
    nvme: "M4 7h16v10H4z M8 11h.01 M4 12h16",
    sata: "M4 5h16v14H4z M7 15h.01 M4 15h16",
    disk: "M4 5h16v14H4z M7 15h.01 M4 15h16",
    sd: "M7 3h7l4 4v14H7z M10 7h1 M13 7h1",
    virtual: "M4 6h16v8H4z M8 18h8 M12 14v4 M8 10h.01",
    image: "M14 3H6a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V8z M14 3v5h5",
    file: "M14 3H6a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V8z M14 3v5h5",
    folder: "M3 7a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z",
    link: "M10 14a4 4 0 0 0 5.6 0l2-2a4 4 0 0 0-5.6-5.6l-1 1 M14 10a4 4 0 0 0-5.6 0l-2 2a4 4 0 0 0 5.6 5.6l1-1",
    refresh: "M20 12a8 8 0 1 1-2.3-5.6 M20 4v5h-5",
    gear: "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3h.1a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8v.1a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z",
    sun: "M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M12 2v2 M12 20v2 M4.9 4.9l1.4 1.4 M17.7 17.7l1.4 1.4 M2 12h2 M20 12h2 M4.9 19.1l1.4-1.4 M17.7 6.3l1.4-1.4",
    moon: "M20.5 14.5A8.5 8.5 0 0 1 9.5 3.5a8.5 8.5 0 1 0 11 11z",
    close: "M6 6l12 12 M18 6L6 18",
    minimize: "M5 12h14",
    maximize: "M5 5h14v14H5z",
    restore: "M8 8V5h11v11h-3 M5 8h11v11H5z",
    chevronRight: "M9 6l6 6-6 6",
    chevronDown: "M6 9l6 6 6-6",
    chevronUp: "M6 15l6-6 6 6",
    plus: "M12 5v14 M5 12h14",
    check: "M5 12l5 5L20 7",
    warning: "M12 3l10 18H2z M12 10v4 M12 17h.01",
    lock: "M6 11h12v10H6z M8 11V7a4 4 0 0 1 8 0v4",
    unlock: "M6 11h12v10H6z M8 11V7a4 4 0 0 1 7.5-2",
    eject: "M5 17h14 M12 4l7 9H5z",
    copy: "M9 9h11v11H9z M5 15H4V4h11v1",
    play: "M7 4l12 8-12 8z",
    hash: "M5 9h14 M5 15h14 M9 4l-2 16 M17 4l-2 16",
    download: "M12 4v12 M7 11l5 5 5-5 M4 20h16",
    upload: "M12 16V4 M7 9l5-5 5 5 M4 20h16",
    trash: "M4 7h16 M9 7V4h6v3 M6 7l1 13h10l1-13 M10 11v6 M14 11v6",
    mount: "M4 6h16v5H4z M4 13h16v5H4z M7 8.5h.01 M7 15.5h.01",
    info: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M12 11v5 M12 8h.01",
    search: "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14z M21 21l-4.3-4.3",
    undo: "M9 14L4 9l5-5 M4 9h11a5 5 0 0 1 0 10h-3",
    dot: "M12 12h.01",
    layers: "M12 3l9 5-9 5-9-5z M3 13l9 5 9-5 M3 17l9 5 9-5",
    scan: "M4 8V5a1 1 0 0 1 1-1h3 M16 4h3a1 1 0 0 1 1 1v3 M20 16v3a1 1 0 0 1-1 1h-3 M8 20H5a1 1 0 0 1-1-1v-3 M4 12h16",
    zap: "M13 2L4 14h7l-1 8 9-12h-7z",
    menu: "M4 7h16 M4 12h16 M4 17h16",
    arrowRight: "M5 12h14 M13 6l6 6-6 6",
    home: "M4 11l8-7 8 7 M6 10v10h12V10",
    volume: "M4 6h16v12H4z M4 12h16 M8 9h.01 M8 15h.01",
    key: "M15 9a4 4 0 1 0-3.9 3.1L4 19v2h3v-2h2v-2h2l1.9-1.9A4 4 0 0 0 15 9z M15 9h.01"
}

// Filled shapes, drawn in the icon colour without a stroke.
// usb: the USB trident (public-domain Wikimedia drawing), rotated upright and fit to the grid.
var filled = {
    usb: "M12 2 L10.72 4.22 L11.63 4.22 L11.63 15.58 L9.3 13.37 C9.15 13.18 9.04 12.93 9.03 12.68 C9.03 11.65 9.03 11.05 9.03 10.82 C9.47 10.67 9.78 10.26 9.78 9.78 C9.78 9.16 9.28 8.67 8.67 8.67 C8.05 8.67 7.56 9.16 7.56 9.78 C7.56 10.26 7.87 10.67 8.3 10.82 L8.3 12.66 C8.3 13.15 8.57 13.68 8.89 14.01 C8.88 14 8.87 13.99 8.89 14.01 C8.9 14.02 11.37 16.35 11.37 16.35 C11.52 16.54 11.63 16.79 11.63 17.04 L11.63 18.33 C10.78 18.5 10.14 19.25 10.14 20.14 C10.14 21.17 10.98 22 12 22 C13.02 22 13.86 21.17 13.86 20.14 C13.86 19.25 13.22 18.5 12.37 18.33 L12.37 17.06 C12.37 17.06 12.37 17.06 12.37 17.05 L12.37 14.26 C12.37 14.01 12.48 13.76 12.63 13.58 C12.63 13.58 15.1 11.24 15.11 11.23 C15.13 11.21 15.12 11.22 15.11 11.23 C15.43 10.9 15.7 10.38 15.7 9.88 L15.7 8.11 L16.44 8.11 L16.44 5.89 L14.22 5.89 L14.22 8.11 L14.97 8.11 C14.97 8.11 14.97 8.58 14.97 9.9 C14.96 10.15 14.85 10.4 14.7 10.59 L12.37 12.8 L12.37 4.22 L13.28 4.22 L12 2z"
}

function svg(name, color, strokeWidth) {
    if (filled[name] !== undefined) {
        var fb = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="' + color + '" stroke="none"><path d="' + filled[name] + '"/></svg>'
        return "data:image/svg+xml;utf8," + encodeURIComponent(fb)
    }
    var d = paths[name] !== undefined ? paths[name] : paths.dot
    var sw = strokeWidth !== undefined ? strokeWidth : 1.6
    var body = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + color
             + '" stroke-width="' + sw + '" stroke-linecap="round" stroke-linejoin="round"><path d="' + d + '"/></svg>'
    return "data:image/svg+xml;utf8," + encodeURIComponent(body)
}
