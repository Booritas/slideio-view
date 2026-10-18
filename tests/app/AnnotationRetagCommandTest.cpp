#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationRetagCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer::app;
namespace core = slideio::viewer::core;

namespace
{

constexpr std::chrono::system_clock::time_point kStamp{std::chrono::seconds{1'700'000'000}};

std::string addMarker(AnnotationModel& model, const std::string& classification)
{
    model.setCurrentCategory(classification);
    return model.add(core::PointGeometry{core::PointF{1.0, 2.0}});
}

} // namespace

TEST_CASE("A retag command applies every change and undoes every change",
          "[app][AnnotationRetagCommand]")
{
    AnnotationModel model;
    const std::string a = addMarker(model, "Mitossis");
    const std::string b = addMarker(model, "Mitossis");
    const std::string other = addMarker(model, "Lymphocyte");

    auto changes = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    REQUIRE(changes.size() == 2);

    auto command = AnnotationRetagCommand::create(model, std::move(changes), "Rename category");
    REQUIRE(command != nullptr);

    command->execute();
    REQUIRE(model.find(a)->properties().classification == "Mitosis");
    REQUIRE(model.find(b)->properties().classification == "Mitosis");
    REQUIRE(model.find(other)->properties().classification == "Lymphocyte");

    command->undo();
    REQUIRE(model.find(a)->properties().classification == "Mitossis");
    REQUIRE(model.find(b)->properties().classification == "Mitossis");
    REQUIRE(model.find(other)->properties().classification == "Lymphocyte");
}

TEST_CASE("A retag of many annotations is one step on the undo stack",
          "[app][AnnotationRetagCommand]")
{
    AnnotationModel model;
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");

    UndoRedoStack stack;
    auto changes = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    stack.push(AnnotationRetagCommand::create(model, std::move(changes), "Rename category"));

    stack.undo();
    REQUIRE(model.annotations()[0].properties().classification == "Mitossis");
    REQUIRE(model.annotations()[2].properties().classification == "Mitossis");

    stack.redo();
    REQUIRE(model.annotations()[0].properties().classification == "Mitosis");
    REQUIRE(model.annotations()[2].properties().classification == "Mitosis");
}

TEST_CASE("A retag command with no changes is not built", "[app][AnnotationRetagCommand]")
{
    // The rule AnnotationEditCommand::create already applies: a command that
    // records nothing would sit on the stack as a step that does nothing, and
    // UndoRedoStack::push already ignores null.
    AnnotationModel model;
    addMarker(model, "Mitosis");

    REQUIRE(AnnotationRetagCommand::create(model, {}, "Rename category") == nullptr);
}

TEST_CASE("restoreMany emits one modelReset rather than one change per annotation",
          "[app][AnnotationModel]")
{
    // Review Focus 5: AnnotationListPanel rebuilds its whole tree on every
    // annotationChanged, so N restores would be N full rebuilds.
    AnnotationModel model;
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");

    int resets = 0;
    int changes = 0;
    QObject::connect(&model, &AnnotationModel::modelReset, &model, [&resets]() { ++resets; });
    QObject::connect(&model, &AnnotationModel::annotationChanged, &model,
                     [&changes](const std::string&) { ++changes; });

    auto retag = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    std::vector<core::Annotation> after;
    for (const core::RetagChange& change : retag) {
        after.push_back(change.after);
    }

    REQUIRE(model.restoreMany(after) == 2);
    REQUIRE(resets == 1);
    REQUIRE(changes == 0);
}

TEST_CASE("restoreMany ignores an annotation the model does not hold",
          "[app][AnnotationModel]")
{
    AnnotationModel model;
    const std::string id = addMarker(model, "Mitosis");

    core::Annotation stranger("not-in-the-model", core::PointGeometry{core::PointF{0.0, 0.0}});

    REQUIRE(model.restoreMany({stranger}) == 0);
    REQUIRE(model.find(id)->properties().classification == "Mitosis");
}
