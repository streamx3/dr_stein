// SPDX-License-Identifier: MIT
#include "preview_decode.hpp"

#include "preview_decode_platform.hpp"
#include "util.hpp"

#include "drstein/core/browse.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImageReader>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace drstein::ui {

#ifndef Q_OS_DARWIN
namespace detail {
PlatformImage platformDecode(std::span<const std::byte>, int, qint64) { return {}; }
} // namespace detail
#endif

namespace {

// QImageReader allocates at most this much for one image (a process-wide limit of its own),
// and only JPEG and SVG decode to a smaller size than the file's. Anything else that would
// need more is not offered to Qt.
constexpr qint64 kQtFullDecodeBytes = 256LL * 1024 * 1024;
// A decode cannot be interrupted once it runs, so a picture is turned down by its header when
// it would take too long: 16384 x 16384, which is 1 GiB at four bytes a pixel.
constexpr qint64 kMaxSourcePixels = 16384LL * 16384;

// An SVG has no pixels of its own, and an icon drawn at its nominal 24 pixels is a speck: a
// small one is drawn up to this size. No further, because rendering costs by the area (a blur
// by more than that) and cannot be interrupted either.
constexpr int kSmallVectorSide = 512;

// A rendering cannot be interrupted, and references multiply: ten <use> of ten <use> of ... draw
// ten to the depth. What an SVG expands to is counted before QtSvg sees it.
constexpr qint64 kMaxSvgElements = 100'000;
// QtSvg draws nested elements and what a reference names by recursion, on the preview worker's
// stack: a chain of 500 <use> overflows macOS's 512 KiB thread stack, 300 do not.
constexpr int kMaxSvgDepth = 128;
constexpr qint64 kMaxSvgCountingSteps = 4'000'000;   // the count itself stays cheap on any file

// The ids an attribute value refers to with url(#id): a style attribute can hold several.
void addUrlTargets(QStringView value, std::vector<QString>& out) {
    for (qsizetype at = value.indexOf(u"url("); at >= 0; at = value.indexOf(u"url(", at + 4)) {
        QStringView rest = value.mid(at + 4).trimmed();
        if (rest.startsWith(u'"') || rest.startsWith(u'\'')) rest = rest.mid(1).trimmed();
        if (!rest.startsWith(u'#')) continue;
        rest = rest.mid(1);
        qsizetype end = 0;
        while (end < rest.size() && rest[end] != u')' && rest[end] != u'"' && rest[end] != u'\'' && !rest[end].isSpace()) ++end;
        if (end > 0) out.push_back(rest.first(end).toString());
    }
}

// Why an SVG is not to be rendered for what it would cost, in plain words; empty when it may be.
// Elements are counted with the ones each reference draws again: <use>, and url(#id) in any
// attribute (masks, clip paths, markers, patterns, filters) except to a gradient, which draws
// nothing of its own. Depth is nesting and references together, as the renderer recurses.
QString svgCostRefusal(std::span<const std::byte> bytes) {
    struct Element {
        qsizetype end = 0;        // one past its last descendant, in document order
        qsizetype firstRef = 0;   // its references in `refs`, which keeps document order too
        int level = 0;            // 1 for a top-level element
        int deepest = 0;          // the deepest level in its subtree
        bool gradient = false;
    };
    struct Ref {
        QString id;
        int level = 0;            // of the element that refers
        qsizetype target = -1;
    };
    std::vector<Element> elements;
    std::vector<Ref> refs;
    std::vector<qsizetype> open;
    QHash<QString, qsizetype> ids;
    auto at = [&](qsizetype i) -> Element& { return elements[static_cast<std::size_t>(i)]; };

    QXmlStreamReader xml(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    auto closeElement = [&] {
        Element& done = at(open.back());
        done.end = static_cast<qsizetype>(elements.size());
        open.pop_back();
        if (!open.empty()) at(open.back()).deepest = std::max(at(open.back()).deepest, done.deepest);
    };
    while (!xml.atEnd() && !xml.hasError()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && !open.empty()) closeElement();
        if (token != QXmlStreamReader::StartElement) continue;
        if (static_cast<qint64>(elements.size()) >= kMaxSvgElements) return QStringLiteral("SVG image of more than %L1 elements: shown as text").arg(kMaxSvgElements);
        const int level = static_cast<int>(open.size()) + 1;
        if (level > kMaxSvgDepth) return QStringLiteral("SVG image nested more than %1 levels deep: shown as text").arg(kMaxSvgDepth);
        const qsizetype index = static_cast<qsizetype>(elements.size());
        const QStringView name = xml.name();
        elements.push_back({index + 1, static_cast<qsizetype>(refs.size()), level, level, name == u"linearGradient" || name == u"radialGradient"});
        open.push_back(index);
        std::vector<QString> named;
        for (const QXmlStreamAttribute& a : xml.attributes()) {
            if (a.name() == u"id") ids.insert(a.value().toString(), index);
            else if (a.name() == u"href" && name == u"use" && a.value().startsWith(u'#')) named.push_back(a.value().mid(1).toString());
            else addUrlTargets(a.value(), named);
        }
        for (QString& id : named) refs.push_back({std::move(id), level, -1});
    }
    // QtSvg renders nothing of a document it cannot parse; what was read so far is counted all the same.
    while (!open.empty()) closeElement();
    for (Ref& r : refs) r.target = ids.value(r.id, -1);
    auto refsBefore = [&](qsizetype element) { return element < static_cast<qsizetype>(elements.size()) ? at(element).firstRef : static_cast<qsizetype>(refs.size()); };

    enum class Mark : unsigned char { Unseen, Open, Done };
    struct Expansion {
        qint64 elements = 0;   // drawing it once draws this many, itself included; at most just over the limit
        int depth = 0;         // levels of recursion that takes, at most just over the limit
    };
    std::vector<Mark> mark(elements.size(), Mark::Unseen);
    std::vector<Expansion> memo(elements.size());
    qint64 steps = 0;
    QString refusal;
    // `calls` deep in itself means a depth of at least that much, so the limit also bounds this recursion.
    std::function<Expansion(qsizetype, int)> expand = [&](qsizetype element, int calls) -> Expansion {
        const auto e = static_cast<std::size_t>(element);
        if (mark[e] == Mark::Done) return memo[e];
        if (mark[e] == Mark::Open) {
            refusal = QStringLiteral("SVG image whose references loop: shown as text");
            return {kMaxSvgElements + 1, kMaxSvgDepth + 1};
        }
        if (calls > kMaxSvgDepth) {
            refusal = QStringLiteral("SVG image whose references nest over %1 levels deep: shown as text").arg(kMaxSvgDepth);
            return {kMaxSvgElements + 1, kMaxSvgDepth + 1};
        }
        mark[e] = Mark::Open;
        const Element& el = at(element);
        Expansion x{el.end - element, el.deepest - el.level + 1};
        for (qsizetype r = el.firstRef, last = refsBefore(el.end); r < last && x.elements <= kMaxSvgElements && x.depth <= kMaxSvgDepth && refusal.isEmpty(); ++r) {
            if (++steps > kMaxSvgCountingSteps) {
                refusal = QStringLiteral("SVG image whose references are too many to follow: shown as text");
                break;
            }
            const Ref& ref = refs[static_cast<std::size_t>(r)];
            if (ref.target < 0 || at(ref.target).gradient) continue;
            const Expansion named = expand(ref.target, calls + 1);
            x.elements += named.elements;
            x.depth = std::max(x.depth, ref.level - el.level + 1 + named.depth);
        }
        mark[e] = Mark::Done;
        memo[e] = {std::min(x.elements, kMaxSvgElements + 1), std::min(x.depth, kMaxSvgDepth + 1)};
        return memo[e];
    };
    Expansion whole;
    for (qsizetype top = 0; top < static_cast<qsizetype>(elements.size()) && whole.elements <= kMaxSvgElements && whole.depth <= kMaxSvgDepth && refusal.isEmpty(); top = at(top).end) {
        const Expansion x = expand(top, 0);
        whole.elements += x.elements;
        whole.depth = std::max(whole.depth, x.depth);
    }
    if (!refusal.isEmpty()) return refusal;
    if (whole.elements > kMaxSvgElements) return QStringLiteral("SVG image that expands to over %L1 elements: shown as text").arg(kMaxSvgElements);
    if (whole.depth > kMaxSvgDepth) return QStringLiteral("SVG image whose references nest over %1 levels deep: shown as text").arg(kMaxSvgDepth);
    return {};
}

qint64 pixelCount(QSize size) { return qint64(size.width()) * size.height(); }

QSize fitted(QSize size, int box) {
    if (size.width() <= box && size.height() <= box) return size;
    return size.scaled(box, box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

QSize vectorSize(QSize nominal, int box) {
    const int side = std::min(box, std::max({nominal.width(), nominal.height(), kSmallVectorSide}));
    return nominal.scaled(side, side, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

void shrinkInto(QImage& image, int box) {
    const QSize fit = fitted(image.size(), box);
    if (fit != image.size()) image = image.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// An icon file holds one picture at several sizes and depths, and Qt reads the first: often
// the smallest. The preview shows the largest, and of those the deepest.
void seekLargestIcon(QImageReader& reader) {
    int best = -1;
    qint64 bestScore = -1;
    const int count = reader.imageCount();
    for (int i = 0; i < count && reader.jumpToImage(i); ++i) {
        const qint64 score = pixelCount(reader.size()) * 64 + QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel();
        if (score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    if (best >= 0) reader.jumpToImage(best);
}

struct QtImage {
    QImage image;
    QSize sourceSize;
    QString format;         // what Qt took the content for; empty when none of its decoders accepts it
    bool tooLarge = false;
};

QtImage decodeWithQt(std::span<const std::byte> bytes, bool vettedSvg, int box) {
    QtImage out;
    QBuffer buffer;
    // Shares the caller's bytes: opened read-only, the buffer never detaches from them.
    buffer.setData(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    if (!buffer.open(QIODevice::ReadOnly)) return out;

    QImageReader reader(&buffer);
    // Named, because the SVG handler looks for <svg in the first 4 KiB only and core's sniff
    // reads past a longer prolog. Every other format is decided from the content.
    if (vettedSvg) reader.setFormat("svg");
    else if (!reader.canRead()) return out;
    const QByteArray format = reader.format().toLower();
    const bool svg = format == "svg" || format == "svgz";
    // QtSvg opens the local files an SVG refers to: one that was not vetted is not rendered.
    if (svg && !vettedSvg) return out;
    out.format = QString::fromLatin1(format);

    reader.setAutoTransform(true);
    if (format == "ico" || format == "cur") seekLargestIcon(reader);
    const QSize stored = reader.size();
    const bool known = stored.isValid() && !stored.isEmpty();
    if (known) out.sourceSize = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90) ? stored.transposed() : stored;

    if (svg || format == "jpeg") {
        if (!known) return out;
        if (!svg && pixelCount(stored) > kMaxSourcePixels) {
            out.tooLarge = true;
            return out;
        }
        reader.setScaledSize(svg ? vectorSize(stored, box) : fitted(stored, box));
    } else if (known && pixelCount(stored) * 4 >= kQtFullDecodeBytes) {
        out.tooLarge = true;
        return out;
    }

    QImage image;
    if (!reader.read(&image) || image.isNull()) return out;
    if (!known) out.sourceSize = image.size();
    shrinkInto(image, box);
    const QImage::Format wanted = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (image.format() != wanted) image.convertTo(wanted);
    out.image = std::move(image);
    return out;
}

// Whether this build of Qt, with the plugins it found, has a decoder for a format core names.
bool qtReads(const QString& id) {
    if (id.isEmpty()) return false;
    const QByteArray name = id == QLatin1String("pnm") ? QByteArray("ppm") : id == QLatin1String("j2k") ? QByteArray("jp2") : id.toLatin1();
    return QImageReader::supportedImageFormats().contains(name);
}

} // namespace

DecodedPreview decodePreview(std::span<const std::byte> bytes, const QString& sniffedFormat, int box) {
    box = std::max(box, 1);
    const bool vettedSvg = sniffedFormat == QLatin1String("svg");
    DecodedPreview out;
    out.format = sniffedFormat;
    if (vettedSvg) {
        out.problem = svgCostRefusal(bytes);
        if (!out.problem.isEmpty()) return out;
    }

    QtImage qt = decodeWithQt(bytes, vettedSvg, box);
    if (!qt.format.isEmpty()) out.format = qt.format;
    out.sourceSize = qt.sourceSize;
    if (!qt.image.isNull()) {
        out.image = std::move(qt.image);
        out.decoder = QStringLiteral("qt");
        return out;
    }

    detail::PlatformImage native;
    if (!vettedSvg && !bytes.empty()) native = detail::platformDecode(bytes, box, kMaxSourcePixels);
    if (!native.format.isEmpty()) out.format = native.format;
    if (native.sourceSize.isValid()) out.sourceSize = native.sourceSize;
    if (!native.image.isNull()) {
        shrinkInto(native.image, box);
        out.image = std::move(native.image);
        out.decoder = QStringLiteral("platform");
        return out;
    }

    const QString label = qs(core::imageFormatLabel(ss(out.format)));
    const QString named = label.isEmpty() ? QStringLiteral("image") : label + QStringLiteral(" image");
    if (native.tooLarge || (qt.tooLarge && !native.recognised)) {
        out.problem = QStringLiteral("%1, %2 × %3: too large to decode")
                          .arg(label.isEmpty() ? QStringLiteral("Image") : named)
                          .arg(out.sourceSize.width())
                          .arg(out.sourceSize.height());
    } else if (qt.format.isEmpty() && !native.recognised && !qtReads(sniffedFormat)) {
        out.problem = label.isEmpty() ? QStringLiteral("not a recognised image") : named + QStringLiteral(": this build has no decoder for it");
    } else {
        out.problem = QStringLiteral("not a readable ") + named;
    }
    return out;
}

} // namespace drstein::ui
