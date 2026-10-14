#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"

#include <string>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

/// Adds a 10x10 annotation at `x0` and returns its id.
std::string addRect(app::AnnotationModel& model, double x0)
{
    return model.add(rect(x0, 0.0, x0 + 10.0, 10.0));
}

} // namespace

TEST_CASE("deleteWithUndo removes the annotation and records one step", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    REQUIRE(app::deleteWithUndo(model, stack, id, "Delete Rectangle"));

    REQUIRE(model.annotations().empty());
    REQUIRE(stack.canUndo());
    REQUIRE(stack.undoDescription() == "Delete Rectangle");
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("undoing a delete puts the annotation back at its original index",
          "[app][AnnotationEditing]")
{
    // The vector's order is paint order and hit-test order both, so an
    // annotation restored at the end would silently start painting on top of
    // everything drawn after it and winning clicks it used to lose.
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string first = addRect(model, 0.0);
    const std::string second = addRect(model, 20.0);
    const std::string third = addRect(model, 40.0);

    REQUIRE(app::deleteWithUndo(model, stack, second, "Delete Rectangle"));
    stack.undo();

    REQUIRE(model.annotations().size() == 3);
    REQUIRE(model.indexOf(first) == 0);
    REQUIRE(model.indexOf(second) == 1);
    REQUIRE(model.indexOf(third) == 2);
}

TEST_CASE("undoing the delete of a selected annotation leaves no selection",
          "[app][AnnotationEditing]")
{
    // Review Focus 2. remove() clears the selection when it named the removed
    // annotation. Undo puts the annotation back, and a selection restored with
    // it would be a selection the user never made -- while leaving a selection
    // naming a removed annotation would be a state setSelected refuses to
    // produce. The annotation comes back; the selection does not.
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);
    model.setSelected(id);
    REQUIRE(model.selectedId() == id);

    REQUIRE(app::deleteWithUndo(model, stack, id, "Delete Rectangle"));
    REQUIRE(model.selectedId().empty());

    stack.undo();
    REQUIRE(model.find(id) != nullptr);
    REQUIRE(model.selectedId().empty());

    stack.redo();
    REQUIRE(model.find(id) == nullptr);
    REQUIRE(model.selectedId().empty());
}

TEST_CASE("deleteWithUndo refuses an id the model does not hold", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    addRect(model, 0.0);

    REQUIRE_FALSE(app::deleteWithUndo(model, stack, "no-such-id", "Delete Rectangle"));
    REQUIRE(model.annotations().size() == 1);
    REQUIRE_FALSE(stack.canUndo());
}

TEST_CASE("setPropertiesWithUndo stores the properties and records one step",
          "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    core::AnnotationProperties properties = model.find(id)->properties();
    properties.label = "Tumour front";
    properties.classification = "Tumor";

    REQUIRE(app::setPropertiesWithUndo(model, stack, id, properties, "Rename annotation"));

    REQUIRE(model.find(id)->properties().label == "Tumour front");
    REQUIRE(stack.undoDescription() == "Rename annotation");
}

TEST_CASE("a properties edit round-trips through undo and redo", "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);

    core::AnnotationProperties properties = model.find(id)->properties();
    properties.label = "Second look";
    REQUIRE(app::setPropertiesWithUndo(model, stack, id, properties, "Rename annotation"));

    stack.undo();
    REQUIRE(model.find(id)->properties().label.empty());

    stack.redo();
    REQUIRE(model.find(id)->properties().label == "Second look");
}

TEST_CASE("an unchanged properties edit records nothing and does not restamp",
          "[app][AnnotationEditing]")
{
    // Tabbing through a field without touching it must not push a step, and
    // must not restamp modifiedAt -- autosave would then write a change that
    // did not happen, which is the defect 9826da6 fixed on the Escape path.
    app::AnnotationModel model;
    app::UndoRedoStack stack;
    const std::string id = addRect(model, 0.0);
    const auto modifiedBefore = model.find(id)->metadata().modifiedAt;

    REQUIRE_FALSE(app::setPropertiesWithUndo(model, stack, id,
                                             model.find(id)->properties(), "Rename annotation"));

    REQUIRE_FALSE(stack.canUndo());
    REQUIRE(model.find(id)->metadata().modifiedAt == modifiedBefore);
}

TEST_CASE("setPropertiesWithUndo refuses an id the model does not hold",
          "[app][AnnotationEditing]")
{
    app::AnnotationModel model;
    app::UndoRedoStack stack;

    REQUIRE_FALSE(app::setPropertiesWithUndo(model, stack, "no-such-id",
                                             core::AnnotationProperties{}, "Rename annotation"));
    REQUIRE_FALSE(stack.canUndo());
}
