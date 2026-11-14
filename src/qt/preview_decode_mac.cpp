// SPDX-License-Identifier: MIT
// macOS: ImageIO reads what Qt has no plugin for here (TIFF, WebP, HEIC, AVIF,
// JPEG 2000, ICNS, camera raw, ...) and decodes a large picture in strips
// straight to thumbnail size, where Qt would need the whole of it in memory.
#include "preview_decode_platform.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>

namespace drstein::ui::detail {

namespace {

struct Release {
    void operator()(CFTypeRef object) const { CFRelease(object); }
};
// Owns one reference from a Create or Copy call.
template <typename Ref>
using Owned = std::unique_ptr<std::remove_pointer_t<Ref>, Release>;

int numberAt(CFDictionaryRef dictionary, CFStringRef key, int fallback) {
    int value = fallback;
    const void* found = dictionary ? CFDictionaryGetValue(dictionary, key) : nullptr;
    if (found && CFGetTypeID(found) == CFNumberGetTypeID()) CFNumberGetValue(static_cast<CFNumberRef>(found), kCFNumberIntType, &value);
    return value;
}

bool flagAt(CFDictionaryRef dictionary, CFStringRef key) {
    const void* found = dictionary ? CFDictionaryGetValue(dictionary, key) : nullptr;
    return found && CFGetTypeID(found) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(found));
}

CFDictionaryRef dictionaryAt(CFDictionaryRef dictionary, CFStringRef key) {
    const void* found = CFDictionaryGetValue(dictionary, key);
    return found && CFGetTypeID(found) == CFDictionaryGetTypeID() ? static_cast<CFDictionaryRef>(found) : nullptr;
}

// Whether ImageIO makes the thumbnail strip by strip, holding little of the picture: measured
// for baseline JPEG, PNG that is not interlaced and HEIF (a 16384 x 16384 one stays near
// 140 MiB). An interlaced PNG, a float TIFF and the formats not measured may be decoded whole.
bool streamsToThumbnail(const QString& type, CFDictionaryRef properties) {
    if (flagAt(properties, kCGImagePropertyIsFloat)) return false;
    if (type == QLatin1String("public.png")) return numberAt(dictionaryAt(properties, kCGImagePropertyPNGDictionary), kCGImagePropertyPNGInterlaceType, 0) == 0;
    return type == QLatin1String("public.jpeg") || type == QLatin1String("public.heic") || type == QLatin1String("public.heif") || type == QLatin1String("public.avif");
}

// What the whole picture takes at its own depth, at least four bytes a pixel.
qint64 wholeBytes(qint64 width, qint64 height, CFDictionaryRef properties) {
    const int depth = std::max(8, numberAt(properties, kCGImagePropertyDepth, 8));
    int channels = 3;
    if (const void* model = CFDictionaryGetValue(properties, kCGImagePropertyColorModel); model && CFGetTypeID(model) == CFStringGetTypeID()) {
        if (CFEqual(model, kCGImagePropertyColorModelGray)) channels = 1;
        else if (CFEqual(model, kCGImagePropertyColorModelCMYK)) channels = 4;
    }
    if (flagAt(properties, kCGImagePropertyHasAlpha)) ++channels;
    return width * height * std::max<qint64>(4, qint64(channels) * ((depth + 7) / 8));
}

} // namespace

PlatformImage platformDecode(std::span<const std::byte> bytes, int box, qint64 maxSourcePixels, qint64 maxWholeBytes) {
    PlatformImage out;
    // No copy and no deallocator: the data object only borrows the caller's bytes, and every
    // object made from it is released before this function returns.
    const Owned<CFDataRef> data(
        CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.data()), static_cast<CFIndex>(bytes.size()), kCFAllocatorNull));
    if (!data) return out;
    const Owned<CGImageSourceRef> source(CGImageSourceCreateWithData(data.get(), nullptr));
    if (!source) return out;
    const CFStringRef type = CGImageSourceGetType(source.get());
    if (!type || CGImageSourceGetCount(source.get()) == 0) return out;
    out.recognised = true;
    if (QString::fromCFString(type).endsWith(QLatin1String("raw-image"))) out.format = QStringLiteral("raw");

    // The primary image: a HEIC file may carry thumbnails and depth maps beside it.
    const size_t index = CGImageSourceGetPrimaryImageIndex(source.get());
    const Owned<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.get(), index, nullptr));
    if (!properties) return out;
    const int width = numberAt(properties.get(), kCGImagePropertyPixelWidth, 0);
    const int height = numberAt(properties.get(), kCGImagePropertyPixelHeight, 0);
    if (width <= 0 || height <= 0) return out;
    // EXIF orientations 5 to 8 turn the picture on its side.
    const bool sideways = numberAt(properties.get(), kCGImagePropertyOrientation, 1) >= 5;
    out.sourceSize = sideways ? QSize(height, width) : QSize(width, height);
    if (qint64(width) * height > maxSourcePixels) {
        out.tooLarge = true;
        return out;
    }
    if (!streamsToThumbnail(QString::fromCFString(type), properties.get()) && wholeBytes(width, height, properties.get()) > maxWholeBytes) {
        out.tooLarge = true;
        return out;
    }

    // Never more than the picture has. Not under 16 either: ImageIO makes no HEIF thumbnail of
    // one or two pixels, and the caller shrinks what comes back larger than its box.
    const int longest = std::min(std::max(box, 16), std::max(width, height));
    const Owned<CFNumberRef> maxPixelSize(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &longest));
    if (!maxPixelSize) return out;
    const void* keys[] = {kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceThumbnailMaxPixelSize, kCGImageSourceCreateThumbnailWithTransform};
    const void* values[] = {kCFBooleanTrue, maxPixelSize.get(), kCFBooleanTrue};
    const Owned<CFDictionaryRef> options(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    if (!options) return out;
    const Owned<CGImageRef> thumbnail(CGImageSourceCreateThumbnailAtIndex(source.get(), index, options.get()));
    if (!thumbnail) return out;

    const size_t w = CGImageGetWidth(thumbnail.get());
    const size_t h = CGImageGetHeight(thumbnail.get());
    if (w == 0 || h == 0) return out;
    const CGImageAlphaInfo alphaInfo = CGImageGetAlphaInfo(thumbnail.get());
    const bool opaque = alphaInfo == kCGImageAlphaNone || alphaInfo == kCGImageAlphaNoneSkipFirst || alphaInfo == kCGImageAlphaNoneSkipLast;

    // Drawn into a QImage's own buffer, as sRGB: nothing of CoreGraphics outlives the call.
    QImage image(static_cast<int>(w), static_cast<int>(h), opaque ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return out;
    image.fill(opaque ? Qt::black : Qt::transparent);
    const Owned<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!srgb) return out;
    const CGBitmapInfo layout = static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Host) |
                                static_cast<CGBitmapInfo>(opaque ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaPremultipliedFirst);
    Owned<CGContextRef> context(CGBitmapContextCreate(image.bits(), w, h, 8, static_cast<size_t>(image.bytesPerLine()), srgb.get(), layout));
    if (!context) return out;
    CGContextDrawImage(context.get(), CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)), thumbnail.get());
    context.reset();
    out.image = std::move(image);
    return out;
}

} // namespace drstein::ui::detail
