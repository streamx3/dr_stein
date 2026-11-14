// SPDX-License-Identifier: MIT
// The operating system's image decoder behind decodePreview(): ImageIO on
// macOS (preview_decode_mac.cpp), none elsewhere.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <cstddef>
#include <span>

namespace drstein::ui::detail {

struct PlatformImage {
    QImage image;             // within the box, upright, owning its pixels; null when not decoded
    QSize sourceSize;         // as displayed; invalid when the header could not be read
    QString format;           // set only where the system knows better than a magic number: "raw" for camera raw
    bool recognised = false;  // the system has a decoder for this content, whether or not it got a picture out
    bool tooLarge = false;    // refused for its pixel count, or for what decoding it whole would hold
};

// Reads `bytes` during the call only. A raster picture is never enlarged. A picture the decoder
// would hold whole to shrink it is refused above `maxWholeBytes`, counted at its own depth.
PlatformImage platformDecode(std::span<const std::byte> bytes, int box, qint64 maxSourcePixels, qint64 maxWholeBytes);

} // namespace drstein::ui::detail
