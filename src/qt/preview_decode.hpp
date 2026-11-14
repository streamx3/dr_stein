// SPDX-License-Identifier: MIT
// From the bytes of an image file to a picture small enough to show. Qt's
// decoders go first; what they cannot read in this build, or refuse for its
// size, goes to the operating system's decoder where there is one (ImageIO on
// macOS). A free function over plain bytes, so the preview worker can run it.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <chrono>
#include <cstddef>
#include <span>

namespace drstein::ui {

struct DecodedPreview {
    QImage image;       // upright, within the box, RGB32 or ARGB32_Premultiplied; null when nothing decoded
    QSize sourceSize;   // the picture's own size, as it is displayed (orientation applied); invalid when unknown
    QString format;     // lowercase id: what Qt found in the content ("jpeg", "png", "ppm"), "raw" for camera raw, else the sniffed one
    QString problem;    // for a null image: why, in plain words
    QString decoder;    // "qt" or "platform" for an image, empty otherwise
    bool retry = false; // the failure is of the moment, not of the file (earlier SVGs still drawing): not to be remembered
};

// Thread-safe, and needs no GUI thread. `sniffedFormat` is core's id for the content ("png",
// "svg", "" when unknown). An SVG is rendered only when that id says "svg": it is what tells
// that the text went through core::svgSafeToRender. Even then it is not rendered, and `problem`
// says why, when it would draw more than 100 000 elements once its references are followed,
// its references loop, or nesting and references go more than 128 levels deep.
// An SVG that passes is drawn on a thread of its own and given up when it has not drawn within
// 2 s; it cannot be interrupted, so it draws on to its end unseen. At most two SVGs draw at once,
// the given-up ones included: past that, one waits for a place within its 2 s, or is not drawn.
// The image fits box x box pixels with its aspect kept. A raster picture is never enlarged. An
// SVG has no pixels of its own: one whose nominal size is under 512 pixels is drawn at 512.
DecodedPreview decodePreview(std::span<const std::byte> bytes, const QString& sniffedFormat, int box = 2048);

// Waits until no SVG draws any more, at most `patience`; false when one still does. For a process
// about to run its static destructors: a drawing that was given up may still be inside QtSvg.
bool waitForSvgDrawings(std::chrono::milliseconds patience);

} // namespace drstein::ui
