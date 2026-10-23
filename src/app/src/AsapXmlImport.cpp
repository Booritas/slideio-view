#include "slideio/viewer/app/AsapXmlImport.h"

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace slideio::viewer::app
{

namespace
{

/// Separates a seed's fields; it cannot occur in an XML attribute ASAP writes.
constexpr char kSeedSeparator = '\x1F';

struct RawCoordinate
{
    std::optional<long long> order;
    std::string x;
    std::string y;
};

/// One <Annotation> as the file wrote it, before anything is interpreted.
struct RawAnnotation
{
    std::string name;
    std::string type;
    std::string group;
    std::string color;
    std::vector<RawCoordinate> coordinates;
};

std::string attribute(const QXmlStreamReader& reader, const char* name)
{
    return reader.attributes().value(QLatin1String(name)).toString().toStdString();
}

/// One coordinate value. A decimal comma -- written by ASAP running under a
/// comma locale -- is read as a decimal point when the value holds a comma and
/// no full stop: an attribute holds one number, so "1234,5" can only mean
/// 1234.5.
///
/// QByteArray::toDouble, which is locale-free, rather than std::from_chars:
/// the floating-point from_chars is missing from the libc++ the macOS builds
/// use, and strtod or a stream would read the C locale, which Qt sets from the
/// user's on Unix -- a decimal comma there.
std::optional<double> parseNumber(const std::string& text)
{
    std::string value = core::trimAsciiWhitespace(text);
    if (!value.empty() && value.front() == '+') {
        value.erase(0, 1);
        if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
            return std::nullopt;
        }
    }
    if (value.find(',') != std::string::npos) {
        if (value.find('.') != std::string::npos) {
            return std::nullopt;
        }
        std::replace(value.begin(), value.end(), ',', '.');
    }
    if (value.empty()) {
        return std::nullopt;
    }
    bool ok = false;
    const double parsed = QByteArray::fromStdString(value).toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

/// ASAP's "#RRGGBB"; alpha opaque.
std::optional<core::Color> parseColor(const std::string& text)
{
    if (text.size() != 7 || text.front() != '#') {
        return std::nullopt;
    }
    unsigned int rgb = 0;
    const auto [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), rgb, 16);
    if (error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return core::Color{static_cast<std::uint8_t>((rgb >> 16) & 0xFF), static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                       static_cast<std::uint8_t>(rgb & 0xFF), 255};
}

/// "Annotation <digits>" exactly: the name ASAP gives every annotation the
/// user did not name, numbered from 0 in every file. It says nothing, and the
/// viewer numbers its own unlabelled rows already.
bool isAutomaticName(const std::string& name)
{
    constexpr std::string_view kPrefix = "Annotation ";
    if (name.size() <= kPrefix.size() || name.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(kPrefix.size()), name.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

/// The coordinates in drawing order: by Order when every coordinate has one,
/// as written otherwise.
std::vector<RawCoordinate> ordered(std::vector<RawCoordinate> coordinates)
{
    const bool allOrdered = std::all_of(coordinates.begin(), coordinates.end(),
                                        [](const RawCoordinate& c) { return c.order.has_value(); });
    if (allOrdered) {
        std::stable_sort(coordinates.begin(), coordinates.end(),
                         [](const RawCoordinate& a, const RawCoordinate& b) { return *a.order < *b.order; });
    }
    return coordinates;
}

std::optional<std::vector<core::PointF>> points(const std::vector<RawCoordinate>& coordinates)
{
    std::vector<core::PointF> out;
    out.reserve(coordinates.size());
    for (const RawCoordinate& coordinate : coordinates) {
        const std::optional<double> x = parseNumber(coordinate.x);
        const std::optional<double> y = parseNumber(coordinate.y);
        if (!x || !y) {
            return std::nullopt;
        }
        out.push_back(core::PointF{*x, *y});
    }
    return out;
}

enum class Kind
{
    Dot,
    PointSet,
    Polygon,
    Rectangle,
    Spline,
    Measurement,
};

std::optional<Kind> kindOf(const std::string& type)
{
    static const std::map<std::string, Kind> kKinds = {
        {"Dot", Kind::Dot},         {"PointSet", Kind::PointSet}, {"Polygon", Kind::Polygon},
        {"Rectangle", Kind::Rectangle}, {"Spline", Kind::Spline}, {"Measurement", Kind::Measurement},
    };
    const auto found = kKinds.find(type);
    return found == kKinds.end() ? std::nullopt : std::optional<Kind>(found->second);
}

/// The shapes one annotation becomes, or nothing when its coordinates cannot
/// make one -- too few for the serializer's minimums, or no finite extent.
std::vector<core::AnnotationGeometry> shapesOf(Kind kind, std::vector<core::PointF> vertices)
{
    std::vector<core::AnnotationGeometry> shapes;
    switch (kind) {
    case Kind::Dot:
        if (!vertices.empty()) {
            shapes.emplace_back(core::PointGeometry{vertices.front()});
        }
        break;
    case Kind::PointSet:
        for (const core::PointF& point : vertices) {
            shapes.emplace_back(core::PointGeometry{point});
        }
        break;
    case Kind::Polygon:
    case Kind::Spline:
        if (vertices.size() >= core::kMinPolygonVertices && core::hasFiniteExtent(vertices)) {
            shapes.emplace_back(core::PolygonGeometry{std::move(vertices), true});
        }
        break;
    case Kind::Rectangle:
        if (vertices.size() >= core::kMinPolygonVertices && core::hasFiniteExtent(vertices)) {
            shapes.push_back(core::closedAreaFrom(std::move(vertices)));
        }
        break;
    case Kind::Measurement:
        if (vertices.size() >= core::kMinPolylineVertices && core::hasFiniteExtent(vertices)) {
            shapes.emplace_back(core::PolygonGeometry{std::move(vertices), false});
        }
        break;
    }
    return shapes;
}

/// What identifies an ASAP annotation, which has no id: its type, its group
/// and its coordinates as the file wrote them.
std::string contentSeed(const RawAnnotation& raw, const std::vector<RawCoordinate>& coordinates)
{
    std::string seed = "asap";
    for (const std::string* field : {&raw.type, &raw.group}) {
        seed += kSeedSeparator;
        seed += *field;
    }
    for (const RawCoordinate& coordinate : coordinates) {
        seed += kSeedSeparator;
        seed += coordinate.x;
        seed += kSeedSeparator;
        seed += coordinate.y;
    }
    return seed;
}

core::AnnotationImportResult failure(std::string message)
{
    core::AnnotationImportResult result;
    result.error = std::move(message);
    return result;
}

core::AnnotationImportResult cancelled()
{
    core::AnnotationImportResult result;
    result.cancelled = true;
    return result;
}

bool cancelRequested(const std::atomic<bool>* cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

} // namespace

core::AnnotationImportResult asapXmlToAnnotations(std::string_view text,
                                                  std::chrono::system_clock::time_point importedAt,
                                                  const std::atomic<bool>* cancel)
{
    // One streaming pass: no tree is built, so depth costs nothing. Groups
    // follow annotations in ASAP's files, so they are resolved afterwards.
    // qsizetype, not int: a file over 2 GB would otherwise reach the reader
    // with a wrapped length.
    QXmlStreamReader reader(QByteArray::fromRawData(text.data(), static_cast<qsizetype>(text.size())));
    std::vector<RawAnnotation> raws;
    std::map<std::string, std::string> groupColors;
    // A stack, not one slot: ASAP never nests an <Annotation> in another, but
    // a file that did would otherwise lose the outer one without a count.
    std::vector<RawAnnotation> open;
    bool sawRoot = false;

    while (!reader.atEnd()) {
        if (cancelRequested(cancel)) {
            return cancelled();
        }
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::DTD) {
            return failure("the file declares a document type, which ASAP files never do");
        }
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (!sawRoot) {
                if (name != QLatin1String("ASAP_Annotations")) {
                    return failure("the file is not an ASAP annotation file");
                }
                sawRoot = true;
            } else if (name == QLatin1String("Annotation")) {
                open.push_back(RawAnnotation{attribute(reader, "Name"), attribute(reader, "Type"),
                                             attribute(reader, "PartOfGroup"), attribute(reader, "Color"), {}});
            } else if (name == QLatin1String("Coordinate") && !open.empty()) {
                RawCoordinate coordinate;
                const std::string order = attribute(reader, "Order");
                long long parsed = 0;
                const auto [end, error] = std::from_chars(order.data(), order.data() + order.size(), parsed);
                if (!order.empty() && error == std::errc() && end == order.data() + order.size()) {
                    coordinate.order = parsed;
                }
                coordinate.x = attribute(reader, "X");
                coordinate.y = attribute(reader, "Y");
                open.back().coordinates.push_back(std::move(coordinate));
            } else if (name == QLatin1String("Group")) {
                groupColors[attribute(reader, "Name")] = attribute(reader, "Color");
            }
        } else if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("Annotation")
                   && !open.empty()) {
            raws.push_back(std::move(open.back()));
            open.pop_back();
        }
    }
    if (reader.hasError()) {
        return failure("the file is not valid XML: " + reader.errorString().toStdString());
    }
    if (!sawRoot) {
        return failure("the file is not an ASAP annotation file");
    }

    core::AnnotationImportResult result;
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(importedAt);
    std::map<std::string, int> repeats;
    for (const RawAnnotation& raw : raws) {
        if (cancelRequested(cancel)) {
            return cancelled();
        }
        ++result.featuresRead;
        const std::optional<Kind> kind = kindOf(raw.type);
        if (!kind) {
            ++result.unsupportedTypes[raw.type];
            continue;
        }
        const std::vector<RawCoordinate> coordinates = ordered(raw.coordinates);
        std::optional<std::vector<core::PointF>> vertices = points(coordinates);
        std::vector<core::AnnotationGeometry> shapes;
        if (vertices) {
            shapes = shapesOf(*kind, std::move(*vertices));
        }
        if (shapes.empty()) {
            ++result.invalidGeometry;
            continue;
        }
        if (*kind == Kind::Spline) {
            ++result.splinesApproximated;
        }

        core::AnnotationProperties properties;
        if (!isAutomaticName(raw.name)) {
            properties.label = raw.name;
        }
        const std::string group = core::trimAsciiWhitespace(raw.group);
        properties.classification = group == "None" ? std::string() : group;
        if (core::isBlankOrUnclassified(properties.classification)) {
            properties.classification.clear();
        }
        std::optional<core::Color> color = parseColor(raw.color);
        if (!color) {
            const auto found = groupColors.find(raw.group);
            if (found != groupColors.end()) {
                color = parseColor(found->second);
            }
        }
        if (color) {
            properties.color = *color;
        }
        core::AnnotationMetadata metadata;
        metadata.createdAt = now;
        metadata.modifiedAt = now;

        // Two identical annotations in one file are still two findings.
        if (*kind == Kind::PointSet) {
            // Each point by where it is, never by its place in the set: ASAP
            // counts cells as one PointSet, and a seed from the whole set
            // would give every point a new id the moment one was added or
            // deleted -- re-importing an edited count would double it. Also
            // what keeps a 50 000-point set from copying all its coordinates
            // into every one of its 50 000 seeds.
            for (std::size_t part = 0; part < shapes.size(); ++part) {
                std::string seed = contentSeed(raw, {coordinates[part]});
                seed += kSeedSeparator + std::to_string(repeats[seed]++);
                result.annotations.emplace_back(std::string(), std::move(shapes[part]), properties, metadata);
                result.idSeeds.push_back(std::move(seed));
            }
        } else {
            std::string seed = contentSeed(raw, coordinates);
            seed += kSeedSeparator + std::to_string(repeats[seed]++);
            result.annotations.emplace_back(std::string(), std::move(shapes.front()), properties, metadata);
            result.idSeeds.push_back(std::move(seed));
        }
        ++result.featuresImported;
    }
    return result;
}

} // namespace slideio::viewer::app
