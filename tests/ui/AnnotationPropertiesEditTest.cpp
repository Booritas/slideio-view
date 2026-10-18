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

TEST_CASE("each field change is individually detected", "[ui][AnnotationProperties]")
{
    // Baseline with non-default values to detect changes in both directions.
    core::AnnotationProperties baseline;
    baseline.label = "Baseline label";
    baseline.classification = "Baseline class";
    baseline.notes = "Baseline notes";
    baseline.color = core::Color{200, 150, 100, 200};
    baseline.lineWidth = 3.0f;
    baseline.fillOpacity = 0.6f;

    // Test each field independently.
    ui::AnnotationFieldValues changed = fieldsFrom(baseline);

    // Change label
    changed.label = "New label";
    auto edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->label == "New label");
    changed = fieldsFrom(baseline);

    // Change classification
    changed.classification = "New classification";
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->classification == "New classification");
    changed = fieldsFrom(baseline);

    // Change notes
    changed.notes = "New notes";
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->notes == "New notes");
    changed = fieldsFrom(baseline);

    // Change color.r
    changed.color.r = 100;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->color.r == 100);
    changed = fieldsFrom(baseline);

    // Change color.g
    changed.color.g = 75;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->color.g == 75);
    changed = fieldsFrom(baseline);

    // Change color.b
    changed.color.b = 50;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->color.b == 50);
    changed = fieldsFrom(baseline);

    // Change color.a
    changed.color.a = 100;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->color.a == 100);
    changed = fieldsFrom(baseline);

    // Change lineWidth (within valid range)
    changed.lineWidth = 2.5f;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->lineWidth == 2.5f);
    changed = fieldsFrom(baseline);

    // Change fillOpacity (within valid range)
    changed.fillOpacity = 0.4f;
    edited = ui::propertiesEdit(baseline, changed);
    REQUIRE(edited.has_value());
    REQUIRE(edited->fillOpacity == 0.4f);
}

TEST_CASE("line width and fill opacity are clamped", "[ui][AnnotationProperties]")
{
    // These values do not only come from the spin box and the slider -- they
    // come from the model, which was loaded from a file a person can edit by
    // hand. A lineWidth of 0 draws nothing and a fillOpacity of 4 is not a
    // colour the renderer can produce.
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

TEST_CASE("in-range clamp values pass through unchanged", "[ui][AnnotationProperties]")
{
    core::AnnotationProperties current;
    current.lineWidth = 3.5f;
    current.fillOpacity = 0.5f;

    const auto edited = ui::propertiesEdit(current, fieldsFrom(current));
    REQUIRE_FALSE(edited.has_value());
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

TEST_CASE("fillOpacity-only change names the style group", "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.fillOpacity = 0.7f;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Restyle annotation");
}

TEST_CASE("color channel changes name recolour", "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;

    // Test green channel
    after.color.g = 100;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Recolour annotation");

    // Test blue channel
    after = before;
    after.color.b = 50;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Recolour annotation");

    // Test alpha channel
    after = before;
    after.color.a = 128;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Recolour annotation");
}

TEST_CASE("style and label changes together get generic description",
          "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.label = "Renamed";
    after.lineWidth = 4.0f;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Edit annotation");
}

TEST_CASE("both style fields changing together is one group",
          "[ui][AnnotationProperties]")
{
    core::AnnotationProperties before;
    core::AnnotationProperties after = before;
    after.lineWidth = 2.5f;
    after.fillOpacity = 0.5f;
    REQUIRE(ui::propertiesEditDescription(before, after) == "Restyle annotation");
}

TEST_CASE("typing Unclassified into Classification clears the field", "[ui][AnnotationProperties]")
{
    // "Unclassified" is the absence of a value; it must never be stored as one.
    core::AnnotationProperties current;
    current.classification = "Mitosis";

    for (const char* typed : {"Unclassified", "unclassified", "  UNCLASSIFIED  "}) {
        ui::AnnotationFieldValues fields = fieldsFrom(current);
        fields.classification = typed;

        const auto edited = ui::propertiesEdit(current, fields);
        REQUIRE(edited.has_value());
        REQUIRE(edited->classification.empty());
    }
}

TEST_CASE("typing Unclassified over an already empty classification is not an edit", "[ui][AnnotationProperties]")
{
    const core::AnnotationProperties current;
    ui::AnnotationFieldValues fields = fieldsFrom(current);
    fields.classification = "Unclassified";

    REQUIRE_FALSE(ui::propertiesEdit(current, fields).has_value());
}
