#include "slideio/viewer/ui/AnnotationGeoJsonImport.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

namespace slideio::viewer::ui
{

namespace
{

QString translate(const char* text)
{
    return QCoreApplication::translate("GeoJsonImport", text);
}

/// English singular and plural chosen here rather than through `%n`: no
/// translation ships, and an untranslated `%n` string shows "annotation(s)".
QString counted(int count, const char* one, const char* many)
{
    return translate(count == 1 ? one : many).arg(count);
}

void addSkipped(QStringList& lines, int count, const char* one, const char* many)
{
    if (count > 0) {
        lines << counted(count, one, many);
    }
}

} // namespace

int countOutsideScene(const std::vector<core::Annotation>& annotations, double width, double height)
{
    int outside = 0;
    for (const core::Annotation& annotation : annotations) {
        const core::RectF box = annotation.boundingBox();
        if (box.x > width || box.y > height || box.x + box.width < 0.0 || box.y + box.height < 0.0) {
            ++outside;
        }
    }
    return outside;
}

QString geoJsonImportSummary(const GeoJsonImportSummaryInput& input)
{
    // Byte escapes, not literal characters: the sources are compiled without
    // /utf-8, so MSVC would read a raw UTF-8 dash in the machine's codepage.
    const core::AnnotationImportResult& result = *input.result;
    QStringList lines;
    if (input.toAdd > 0) {
        lines << translate("Import annotations from %1?").arg(input.fileName) << QString();
        lines << counted(input.toAdd, "%1 annotation will be added.", "%1 annotations will be added.");
    } else {
        lines << translate("Nothing in %1 can be imported.").arg(input.fileName) << QString();
    }

    addSkipped(lines, input.alreadyPresent, "%1 already in this scene (same id) \xE2\x80\x94 skipped.",
               "%1 already in this scene (same id) \xE2\x80\x94 skipped.");
    for (const auto& [kind, count] : result.skippedObjectTypes) {
        lines << counted(count,
                         "%1 object of type \"%2\" \xE2\x80\x94 skipped (only annotations are imported).",
                         "%1 objects of type \"%2\" \xE2\x80\x94 skipped (only annotations are imported).")
                     .arg(QString::fromStdString(kind));
    }
    addSkipped(lines, result.nestedObjects, "%1 nested object \xE2\x80\x94 skipped.",
               "%1 nested objects \xE2\x80\x94 skipped.");
    addSkipped(lines, result.withHoles, "%1 polygon with holes \xE2\x80\x94 skipped.",
               "%1 polygons with holes \xE2\x80\x94 skipped.");
    addSkipped(lines, result.unsupportedGeometry, "%1 with an unsupported geometry \xE2\x80\x94 skipped.",
               "%1 with an unsupported geometry \xE2\x80\x94 skipped.");
    addSkipped(lines, result.invalidGeometry, "%1 with invalid coordinates \xE2\x80\x94 skipped.",
               "%1 with invalid coordinates \xE2\x80\x94 skipped.");

    QStringList warnings;
    const core::AnnotationImportProvenance& from = result.provenance;
    if (from.present) {
        const QString slide = QFileInfo(QString::fromStdString(from.slide)).fileName();
        const bool otherSlide =
            !from.slideId.empty() && !input.openSlideId.empty() && from.slideId != input.openSlideId;
        const bool otherScene = from.scene >= 0 && from.scene != input.openScene;
        if (otherSlide) {
            warnings << (from.scene >= 0
                             ? translate("This file was exported from a different slide: scene %1 of \"%2\".")
                                   .arg(QString::number(from.scene), slide)
                             : translate("This file was exported from a different slide, \"%1\".").arg(slide));
        } else if (otherScene) {
            // One multi-argument arg(), not a chain: the slide name comes
            // from the file, and a chained .arg() would substitute a %3 in it.
            warnings << translate("This file was exported from scene %1 of \"%2\"; the open scene is scene %3.")
                            .arg(QString::number(from.scene), slide, QString::number(input.openScene));
        }
    }
    if (input.outsideScene > 0) {
        warnings << counted(input.outsideScene, "%1 of the annotations lies entirely outside this scene.",
                            "%1 of the annotations lie entirely outside this scene.");
    }
    if (!warnings.isEmpty()) {
        lines << QString();
        for (const QString& warning : warnings) {
            lines << QString::fromUtf8("\xE2\x9A\xA0 ") + warning;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace slideio::viewer::ui
