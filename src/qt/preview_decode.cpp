// SPDX-License-Identifier: MIT
#include "preview_decode.hpp"

#include "preview_decode_platform.hpp"
#include "util.hpp"

#include "drstein/core/browse.hpp"
#include "drstein/core/deadline.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImageReader>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace drstein::ui {

#ifndef Q_OS_DARWIN
namespace detail {
PlatformImage platformDecode(std::span<const std::byte>, int, qint64, qint64) { return {}; }
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
// What a platform decoder that holds the whole picture may hold of it: a 16384 x 16384 float
// TIFF took 8 GiB in ImageIO.
constexpr qint64 kMaxWholeDecodeBytes = 512LL * 1024 * 1024;

// An SVG has no pixels of its own, and an icon drawn at its nominal 24 pixels is a speck: a
// small one is drawn up to this size. No further, because rendering costs by the area (a blur
// by more than that) and cannot be interrupted either.
constexpr int kSmallVectorSide = 512;
// ... and a large one only to this size: the preview pane is at most 560 points wide, and
// fills, masks and blurs cost by the area drawn.
constexpr int kLargeVectorSide = 1024;

// A rendering cannot be interrupted, and references multiply: ten <use> of ten <use> of ... draw
// ten to the depth. What an SVG expands to is counted before QtSvg sees it.
constexpr qint64 kMaxSvgElements = 100'000;
// QtSvg draws nested elements and what a reference names by recursion, on the preview worker's
// stack: a chain of 500 <use> overflows macOS's 512 KiB thread stack, 300 do not.
constexpr int kMaxSvgDepth = 128;
constexpr qint64 kMaxSvgCountingSteps = 4'000'000;   // the count itself stays cheap on any file

// No count bounds what drawing an SVG costs (one self-crossing path of 90 000 segments took
// 251 s), and QtSvg cannot be interrupted: a drawing is waited for this long, then given up as a
// failure and left to end on its own thread.
constexpr std::chrono::milliseconds kSvgDrawDeadline{2000};
// Drawings at once, the given-up ones included: each keeps a core busy until it ends.
constexpr std::size_t kSvgDrawPlaces = 2;

// Never destroyed: a drawing that was given up gives its place back whenever it ends.
core::DeadlineRunner& svgDrawings() {
    static auto* runner = new core::DeadlineRunner(kSvgDrawPlaces);
    return *runner;
}

// What a style sheet may cost to apply: QtSvg matches every rule against every element.
constexpr qint64 kMaxSvgStyleMatches = 10'000'000;
// A mask or a filter is drawn into a layer of its own the size of the picture, then composed:
// one application costs as much as this many plain elements (a blur over the whole picture
// took about a second at 2048 px).
constexpr qint64 kSvgLayerCost = 2'000;

// The ids an attribute value or a style sheet refers to with url(#id); QtSvg reads "url (" as
// "url(" too. A style attribute can hold several.
void addUrlTargets(QStringView value, std::vector<QString>& out) {
    for (qsizetype at = value.indexOf(u"url", 0, Qt::CaseInsensitive); at >= 0; at = value.indexOf(u"url", at + 3, Qt::CaseInsensitive)) {
        qsizetype i = at + 3;
        while (i < value.size() && value[i].isSpace()) ++i;
        if (i >= value.size() || value[i] != u'(') continue;
        QStringView rest = value.mid(i + 1).trimmed();
        if (rest.startsWith(u'"') || rest.startsWith(u'\'')) rest = rest.mid(1).trimmed();
        if (!rest.startsWith(u'#')) continue;
        rest = rest.mid(1);
        qsizetype end = 0;
        while (end < rest.size() && rest[end] != u')' && rest[end] != u'"' && rest[end] != u'\'' && !rest[end].isSpace()) ++end;
        if (end > 0) out.push_back(rest.first(end).toString());
    }
}

// The points of a path or a polyline, where markers are drawn: at most one per number in it.
qint64 vertexCount(QStringView data) {
    qint64 numbers = 0;
    bool inNumber = false;
    for (const QChar c : data) {
        const bool digit = c.isDigit();
        if (digit && !inNumber) ++numbers;
        inNumber = digit || (inNumber && c == u'.');
    }
    return std::max<qint64>(1, numbers);
}

// One rule of a style sheet as far as references go: its selectors and the ids it refers to.
struct StyleRule {
    std::vector<QString> selectors;
    std::vector<QString> targets;
};

std::vector<StyleRule> styleRules(QStringView sheet, qint64& ruleCount) {
    std::vector<StyleRule> rules;
    ruleCount = 0;
    qsizetype at = 0;
    while (at < sheet.size()) {
        const qsizetype open = sheet.indexOf(u'{', at);
        if (open < 0) break;
        qsizetype close = sheet.indexOf(u'}', open + 1);
        if (close < 0) close = sheet.size();
        ++ruleCount;
        StyleRule rule;
        addUrlTargets(sheet.mid(open + 1, close - open - 1), rule.targets);
        if (!rule.targets.empty()) {
            for (QStringView selector : sheet.mid(at, open - at).split(u','))
                rule.selectors.push_back(selector.trimmed().toString());
            rules.push_back(std::move(rule));
        }
        at = close + 1;
    }
    return rules;
}

struct Seen {
    QString name, id;
    QStringList classes;
};

// Whether a selector may match an element. Plain ones ("rect", ".a", "#b", "rect.a", "*") are
// decided; anything else is taken to match, which can only overcount.
bool mayMatch(const QString& selector, const Seen& e) {
    if (selector.isEmpty()) return false;
    if (selector == u"*") return true;
    auto plain = [](QStringView word) {
        if (word.isEmpty()) return false;
        for (const QChar c : word)
            if (!(c.isLetterOrNumber() || c == u'-' || c == u'_')) return false;
        return true;
    };
    if (selector.startsWith(u'#') && plain(QStringView(selector).mid(1))) return QStringView(selector).mid(1) == e.id;
    const qsizetype dot = selector.indexOf(u'.');
    const QStringView tag = dot < 0 ? QStringView(selector) : QStringView(selector).first(dot);
    const QStringView cls = dot < 0 ? QStringView() : QStringView(selector).mid(dot + 1);
    if ((!tag.isEmpty() && !plain(tag)) || (dot >= 0 && !plain(cls))) return true;
    if (!tag.isEmpty() && tag != e.name) return false;
    return dot < 0 || e.classes.contains(cls.toString());
}

// Why an SVG is not to be rendered for what it would cost, in plain words; empty when it may be.
// Elements are counted with the ones each reference draws again: <use>, and url(#id) in any
// attribute or style sheet rule (masks, clip paths, markers, patterns, filters) except to a
// gradient, which draws nothing of its own. A mask or a filter adds the cost of a layer; a
// marker is drawn at every point of its path. An id defined twice is counted at each
// definition: QtSvg may take either. Depth is nesting and references together, as the
// renderer recurses.
QString svgCostRefusal(std::span<const std::byte> bytes) {
    enum class Kind : unsigned char { Plain, Gradient, Layer, Marker };
    struct Element {
        qsizetype end = 0;        // one past its last descendant, in document order
        qsizetype firstRef = 0;   // its references in `refs`, which keeps document order too
        int level = 0;            // 1 for a top-level element
        int deepest = 0;          // the deepest level in its subtree
        Kind kind = Kind::Plain;
        qint64 vertices = 1;
    };
    struct Ref {
        QString id;
        qsizetype owner = 0;      // the element that refers
        std::vector<qsizetype> targets;
    };
    std::vector<Element> elements;
    std::vector<Seen> seen;
    std::vector<std::vector<QString>> named;   // per element, the ids its attributes refer to
    std::vector<qsizetype> open;
    std::vector<bool> openIsStyle;
    QHash<QString, std::vector<qsizetype>> ids;
    QString styleSheet;
    int inStyle = 0;
    auto at = [&](qsizetype i) -> Element& { return elements[static_cast<std::size_t>(i)]; };

    QXmlStreamReader xml(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    auto closeElement = [&] {
        Element& done = at(open.back());
        done.end = static_cast<qsizetype>(elements.size());
        open.pop_back();
        if (openIsStyle.back()) --inStyle;
        openIsStyle.pop_back();
        if (!open.empty()) at(open.back()).deepest = std::max(at(open.back()).deepest, done.deepest);
    };
    while (!xml.atEnd() && !xml.hasError()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::Characters && inStyle) styleSheet += xml.text();
        if (token == QXmlStreamReader::EndElement && !open.empty()) closeElement();
        if (token != QXmlStreamReader::StartElement) continue;
        if (static_cast<qint64>(elements.size()) >= kMaxSvgElements) return QStringLiteral("SVG image of more than %L1 elements: shown as text").arg(kMaxSvgElements);
        const int level = static_cast<int>(open.size()) + 1;
        if (level > kMaxSvgDepth) return QStringLiteral("SVG image nested more than %1 levels deep: shown as text").arg(kMaxSvgDepth);
        const qsizetype index = static_cast<qsizetype>(elements.size());
        const QStringView name = xml.name();
        Element e{index + 1, 0, level, level, Kind::Plain, 1};
        if (name == u"linearGradient" || name == u"radialGradient") e.kind = Kind::Gradient;
        else if (name == u"mask" || name == u"filter") e.kind = Kind::Layer;
        else if (name == u"marker") e.kind = Kind::Marker;
        Seen s{name.toString(), {}, {}};
        std::vector<QString> refers;
        for (const QXmlStreamAttribute& a : xml.attributes()) {
            if (a.name() == u"id") {
                s.id = a.value().toString();
                ids[s.id].push_back(index);
            } else if (a.name() == u"class") {
                s.classes = a.value().toString().split(QChar(u' '), Qt::SkipEmptyParts);
            } else if (a.name() == u"href" && name == u"use" && a.value().startsWith(u'#')) {
                refers.push_back(a.value().mid(1).toString());
            } else {
                if (a.name() == u"d" || a.name() == u"points") e.vertices = vertexCount(a.value());
                addUrlTargets(a.value(), refers);
            }
        }
        elements.push_back(e);
        seen.push_back(std::move(s));
        named.push_back(std::move(refers));
        open.push_back(index);
        openIsStyle.push_back(name == u"style");
        if (name == u"style") ++inStyle;
    }
    // QtSvg renders nothing of a document it cannot parse; what was read so far is counted all the same.
    while (!open.empty()) closeElement();

    // The style sheet's references go to the elements its rules may match; never to one inside
    // the very element referred to, which would only make a loop of what QtSvg does not draw.
    qint64 ruleCount = 0;
    const std::vector<StyleRule> rules = styleRules(styleSheet, ruleCount);
    if (ruleCount * static_cast<qint64>(elements.size()) > kMaxSvgStyleMatches) return QStringLiteral("SVG image whose style sheet would take too long to apply: shown as text");
    for (const StyleRule& rule : rules)
        for (std::size_t e = 0; e < elements.size(); ++e) {
            if (!std::any_of(rule.selectors.begin(), rule.selectors.end(), [&](const QString& sel) { return mayMatch(sel, seen[e]); })) continue;
            for (const QString& id : rule.targets) {
                const auto definitions = ids.value(id);
                const bool inside = std::any_of(definitions.begin(), definitions.end(), [&](qsizetype t) { return t <= static_cast<qsizetype>(e) && static_cast<qsizetype>(e) < at(t).end; });
                if (!inside) named[e].push_back(id);
            }
        }

    std::vector<Ref> refs;
    for (std::size_t e = 0; e < elements.size(); ++e) {
        elements[e].firstRef = static_cast<qsizetype>(refs.size());
        for (QString& id : named[e]) {
            Ref r{std::move(id), static_cast<qsizetype>(e), {}};
            r.targets = ids.value(r.id);
            refs.push_back(std::move(r));
        }
    }
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
            const Ref& ref = refs[static_cast<std::size_t>(r)];
            const Element& owner = at(ref.owner);
            for (const qsizetype target : ref.targets) {
                if (++steps > kMaxSvgCountingSteps) {
                    refusal = QStringLiteral("SVG image whose references are too many to follow: shown as text");
                    break;
                }
                const Element& t = at(target);
                if (t.kind == Kind::Gradient) continue;
                const Expansion drawn = expand(target, calls + 1);
                const qint64 times = t.kind == Kind::Marker ? owner.vertices : 1;
                x.elements += std::min(drawn.elements, kMaxSvgElements + 1) * std::min(times, kMaxSvgElements + 1) + (t.kind == Kind::Layer ? kSvgLayerCost : 0);
                x.elements = std::min(x.elements, kMaxSvgElements + 1);
                x.depth = std::max(x.depth, owner.level - el.level + 1 + drawn.depth);
                if (!refusal.isEmpty()) break;
            }
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
    const int side = std::min({box, kLargeVectorSide, std::max({nominal.width(), nominal.height(), kSmallVectorSide})});
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
    // What was decoded at full size is the picture's size, whatever its header claimed: an icon
    // directory or a GIF screen can name a size the picture inside does not have.
    if (!known || !(svg || format == "jpeg")) out.sourceSize = image.size();
    // In place first, then scaled: scaling a picture not premultiplied makes a premultiplied
    // copy of the whole of it.
    const QImage::Format wanted = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (image.format() != wanted) image.convertTo(wanted);
    shrinkInto(image, box);
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

    QtImage qt;
    if (vettedSvg) {
        // On a thread of its own (std::thread's stack, the same 512 KiB on macOS as the preview
        // worker's, for which kMaxSvgDepth was measured), with its own copy of the source: a
        // drawing given up outlives this call and the bytes it was handed.
        auto source = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        auto drawn = std::make_shared<QtImage>();
        switch (svgDrawings().run([source, drawn, box] { *drawn = decodeWithQt(*source, true, box); }, kSvgDrawDeadline)) {
        case core::DeadlineRunner::Outcome::Finished: qt = std::move(*drawn); break;
        case core::DeadlineRunner::Outcome::TimedOut:
            out.problem = QStringLiteral("SVG image that did not draw within %1 s: shown as text").arg(kSvgDrawDeadline.count() / 1000);
            return out;
        case core::DeadlineRunner::Outcome::Busy:
            out.problem = QStringLiteral("SVG image not drawn: earlier ones are still drawing");
            out.retry = true;
            return out;
        }
    } else {
        qt = decodeWithQt(bytes, false, box);
    }
    if (!qt.format.isEmpty()) out.format = qt.format;
    out.sourceSize = qt.sourceSize;
    if (!qt.image.isNull()) {
        out.image = std::move(qt.image);
        out.decoder = QStringLiteral("qt");
        return out;
    }

    detail::PlatformImage native;
    if (!vettedSvg && !bytes.empty()) native = detail::platformDecode(bytes, box, kMaxSourcePixels, kMaxWholeDecodeBytes);
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

bool waitForSvgDrawings(std::chrono::milliseconds patience) { return svgDrawings().waitUntilIdle(patience); }

} // namespace drstein::ui
