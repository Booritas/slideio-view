#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <atomic>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace slideio::viewer::core
{

/// What a file said about where it came from -- present only in a file this
/// application exported. ASAP files never carry it.
struct AnnotationImportProvenance
{
    bool present = false;
    std::string slide;
    std::string slideId;
    /// -1 when the file does not say.
    int scene = -1;
};

/// What an annotation file turned into, and an account of everything it did
/// not -- whichever format it was. The GeoJSON reader and the ASAP XML reader
/// both fill it, so the summary, the planning and the dialog take one type.
///
/// Every top-level object is either imported (one annotation, or one per part
/// of a multi-part geometry) or counted under exactly one skip reason, so
/// `featuresRead == featuresImported + every per-object skip count` and nothing
/// leaves the file unreported.
struct AnnotationImportResult
{
    /// In file order. An EMPTY id means the caller must mint one: core is
    /// Qt-free and cannot make a UUID.
    std::vector<Annotation> annotations;
    /// Parallel to `annotations`. Non-empty where an annotation's identity is
    /// derived from its content rather than given by the file: each part of a
    /// GeoJSON multi-part geometry whose feature has an id, and every ASAP
    /// annotation, which has no id at all. The caller turns it into a
    /// version-5 UUID, so the same file imported twice derives the same ids.
    ///
    /// For a GeoJSON part it is the feature's id plus the part's own
    /// coordinates, not its position in the list: QuPath writes a whole Points
    /// class as ONE feature and keeps its id when the class is edited, so after
    /// a point is deleted every later position names a different point.
    std::vector<std::string> idSeeds;
    int featuresRead = 0;
    /// Objects that produced at least one annotation.
    int featuresImported = 0;
    /// Objects QuPath marks as something other than an annotation --
    /// "detection", "cell", "tile" -- counted under the value it gives.
    std::map<std::string, int> skippedObjectTypes;
    /// Shapes of a type the viewer has no counterpart for, by the file's own
    /// name for the type -- an ASAP Type it does not know.
    std::map<std::string, int> unsupportedTypes;
    /// A polygon with an interior ring, which the viewer has no shape for.
    int withHoles = 0;
    /// A GeometryCollection, a null geometry, or a type GeoJSON does not define.
    int unsupportedGeometry = 0;
    /// Coordinates that are not finite numbers, or too few of them for the
    /// shape -- the serializer's own minimums, so every annotation returned
    /// is one the application can save.
    int invalidGeometry = 0;
    /// QuPath `childObjects`, counted recursively. Not objects of the file's
    /// top level, so outside the accounting identity above.
    int nestedObjects = 0;
    /// ASAP splines, imported as polygons through their control points. They
    /// ARE imported -- this counts an approximation the summary owes the user,
    /// not a skip.
    int splinesApproximated = 0;
    AnnotationImportProvenance provenance;
    /// True when the read was stopped through its cancel flag: every other
    /// field is then empty, and nothing should be reported -- the user asked
    /// for it.
    bool cancelled = false;
    /// Non-empty when the file could not be read at all: then every other
    /// field is empty.
    std::string error;
};

/// Which reader a file needs.
enum class AnnotationFileFormat
{
    GeoJson,
    AsapXml,
};

/// By content, never by extension: after an optional UTF-8 byte-order mark
/// and ASCII whitespace, a '<' is XML and anything else is handed to the
/// GeoJSON reader, which reports what it cannot read. A `.json` that is really
/// XML, or the reverse, is read correctly. UTF-16 is recognised too -- a
/// UTF-16 byte-order mark, or a '<' beside a zero byte -- since Qt's XML reader
/// decodes it and an ASAP file re-saved by another tool may be in it.
[[nodiscard]] AnnotationFileFormat detectAnnotationFileFormat(std::string_view text);

/// A result for a file that could not be read at all: `error` set, every other
/// field empty. Shared by the readers so the contract has one definition.
[[nodiscard]] AnnotationImportResult importFailure(std::string message);

/// A result for a read the user cancelled: `cancelled` set, nothing else.
[[nodiscard]] AnnotationImportResult cancelledImport();

/// Whether a reader's cancel flag -- optional, so null means never -- has been
/// raised. Relaxed: a cancel only has to be noticed soon, not ordered.
[[nodiscard]] bool importCancelRequested(const std::atomic<bool>* cancel);

/// The smallest and largest corner of `points`, taken as they are -- never
/// rebuilt as origin plus extent, which can overflow where the corners do not.
/// Precondition: `points` is not empty.
[[nodiscard]] std::pair<PointF, PointF> cornersOf(const std::vector<PointF>& points);

/// False when the shape's width or height is not a finite number although
/// every coordinate is -- corners near the edge of the double range. The
/// outline, the bounding box and measure() all subtract, so such a shape would
/// paint, hit-test and export infinities and NaNs. True for no points.
[[nodiscard]] bool hasFiniteExtent(const std::vector<PointF>& points);

/// The four corners of an axis-aligned rectangle, as QuPath writes its
/// Rectangle ROI, ASAP its Rectangle, and the export a rectangle: four distinct
/// points, two distinct x and two distinct y, every edge horizontal or
/// vertical.
[[nodiscard]] bool isAxisAlignedBox(const std::vector<PointF>& ring);

/// A closed ring as the viewer's shape for it: a rectangle when it is an
/// axis-aligned box, so it gets resize handles; a closed polygon otherwise.
[[nodiscard]] AnnotationGeometry closedAreaFrom(std::vector<PointF> ring);

} // namespace slideio::viewer::core
