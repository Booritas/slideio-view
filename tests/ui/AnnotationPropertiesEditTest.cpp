#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"

#include <string>

using namespace slideio::viewer;

namespace
{

ui::AnnotationFieldValues fieldsFrom(const core::AnnotationProperties& properties)
{
    ui::AnnotationFieldValues fields;
    fields.label = properties.label;
    fields.classification = properties.classification;
    fields.notes = properties.notes;
    fields.color = properties.color;
    fields.lineWidth = properties.lineWidth;
    fields.fillOpacity = properties.fillOpacity;
    return fields;
}

} // namespace

TEST_CASE("an unchanged field set is not an edit", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    REQUIRE_FALSE(ui::propertiesEdit(current, fieldsFrom(current)).has_value());
}

TEST_CASE("a changed label is an edit", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    ui::AnnotationFieldValues fields = fieldsFrom(current);
    fields.label = "Tumour front";

    const auto edited = ui::propertiesEdit(current, fields);
    REQUIRE(edited.has_value());
    REQUIRE(edited->label == "Tumour front");
}

TEST_CASE("line width and fill opacity are clamped", "[ui][AnnotationProperties]")
{
    // Review Focus 4. These values do not only come from the spin box and the
    // slider -- they come from the model, which was loaded from a file a person
    // can edit by hand. A lineWidth of 0 draws nothing and a fillOpacity of 4
    // is not a colour the renderer can produce.
    core::AnnotationProperties current;
    current.lineWidth = 0.0f;
    current.fillOpacity = 4.0f;

    const auto edited = ui::propertiesEdit(current, fieldsFrom(current));
    REQUIRE(edited.has_value());
    REQUIRE(edited->lineWidth == 1.0f);
    REQUIRE(edited->fillOpacity == 1.0f);

    core::AnnotationProperties negative;
    negative.lineWidth = 99.0f;
    negative.fillOpacity = -2.0f;

    const auto fixed = ui::propertiesEdit(negative, fieldsFrom(negative));
    REQUIRE(fixed.has_value());
    REQUIRE(fixed->lineWidth == 5.0f);
    REQUIRE(fixed->fillOpacity == 0.0f);
}

TEST_CASE("a free-text classification is kept", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    ui::AnnotationFieldValues fields = fieldsFrom(current);
    fields.classification = "Lymphovascular invasion";

    const auto edited = ui::propertiesEdit(current, fields);
    REQUIRE(edited.has_value());
    REQUIRE(edited->classification == "Lymphovascular invasion");
}

TEST_CASE("the five offered classifications are the agreed set", "[ui][AnnotationProperties]")
{
    REQUIRE(ui::kAnnotationClassifications.size() == 5);
    REQUIRE(ui::kAnnotationClassifications[0] == std::string("Tumor"));
    REQUIRE(ui::kAnnotationClassifications[1] == std::string("Stroma"));
    REQUIRE(ui::kAnnotationClassifications[2] == std::string("Necrosis"));
    REQUIRE(ui::kAnnotationClassifications[3] == std::string("Normal"));
    REQUIRE(ui::kAnnotationClassifications[4] == std::string("Artifact"));
}

TEST_CASE("the edit description names the field that changed",
          "[ui][AnnotationProperties]")
{
    // The Edit menu renders undoDescription(). Without this every properties
    // edit reads identically and two of them cannot be told apart on the stack.
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.label = "Renamed";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Rename annotation");

    after = before;
    after.classification = "Tumor";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Reclassify annotation");

    after = before;
    after.color = core::Color{1, 2, 3, 255};
    REQUIRE(ui::propertiesEditDescription(before, after) == "Recolour annotation");

    after = before;
    after.notes = "Mitoses";
    REQUIRE(ui::propertiesEditDescription(before, after) == "Edit annotation notes");

    after = before;
    after.lineWidth = 4.0f;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Restyle annotation");
}

TEST_CASE("a multi-field change gets the generic description",
          "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.label = "Renamed";
    after.classification = "Tumor";

    REQUIRE(ui::propertiesEditDescription(before, after) == "Edit annotation");
}
