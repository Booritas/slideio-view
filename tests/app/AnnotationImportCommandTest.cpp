#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationEditing.h"
#include "slideio/viewer/app/AnnotationImportCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <chrono>
#include <set>
#include <string>
#include <vector>

using namespace slideio::viewer::app;
namespace core = slideio::viewer::core;

namespace
{

core::Annotation imported(std::string id, double x)
{
    core::AnnotationProperties properties;
    properties.label = "imported " + id;
    core::AnnotationMetadata metadata;
    metadata.author = "qupath user";
    metadata.createdAt = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
    metadata.modifiedAt = metadata.createdAt;
    return core::Annotation{std::move(id), core::PointGeometry{core::PointF{x, 1.0}}, properties, metadata};
}

std::vector<std::string> idsOf(const AnnotationModel& model)
{
    std::vector<std::string> ids;
    for (const core::Annotation& annotation : model.annotations()) {
        ids.push_back(annotation.id());
    }
    return ids;
}

bool noDuplicateIds(const AnnotationModel& model)
{
    const std::vector<std::string> ids = idsOf(model);
    return std::set<std::string>(ids.begin(), ids.end()).size() == ids.size();
}

/// Imports {a, b} into the model the way MainWindow does: the model changes
/// first, then the command is recorded.
void importAB(AnnotationModel& model, UndoRedoStack& stack)
{
    const std::vector<core::Annotation> added = {imported("a", 1.0), imported("b", 2.0)};
    model.insertMany(added);
    stack.push(AnnotationImportCommand::create(model, added, "Import 2 annotations"));
}

} // namespace

TEST_CASE("an import command with nothing to add is null", "[app][AnnotationImportCommand]")
{
    AnnotationModel model;
    REQUIRE(AnnotationImportCommand::create(model, {}, "Import 0 annotations") == nullptr);
}

TEST_CASE("undoing an import removes exactly the imported annotations", "[app][AnnotationImportCommand]")
{
    AnnotationModel model;
    model.insert(imported("x", 0.0));
    UndoRedoStack stack;
    importAB(model, stack);
    REQUIRE(idsOf(model) == std::vector<std::string>{"x", "a", "b"});

    stack.undo();

    REQUIRE(idsOf(model) == std::vector<std::string>{"x"});
}

TEST_CASE("redoing an import puts the annotations back at the end", "[app][AnnotationImportCommand]")
{
    AnnotationModel model;
    model.insert(imported("x", 0.0));
    UndoRedoStack stack;
    importAB(model, stack);
    stack.undo();

    stack.redo();

    REQUIRE(idsOf(model) == std::vector<std::string>{"x", "a", "b"});
    const core::Annotation* a = model.find("a");
    REQUIRE(a != nullptr);
    REQUIRE(a->properties().label == "imported a");
    REQUIRE(a->metadata().author == "qupath user");
    REQUIRE(a->metadata().createdAt == imported("a", 1.0).metadata().createdAt);
}

TEST_CASE("an import undone after a later edit leaves that edit's undo first", "[app][AnnotationImportCommand]")
{
    AnnotationModel model;
    model.insert(imported("x", 0.0));
    UndoRedoStack stack;
    importAB(model, stack);
    REQUIRE(deleteWithUndo(model, stack, "x", "Delete marker"));
    REQUIRE(idsOf(model) == std::vector<std::string>{"a", "b"});

    stack.undo();
    REQUIRE(idsOf(model) == std::vector<std::string>{"x", "a", "b"});
    REQUIRE(noDuplicateIds(model));

    stack.undo();
    REQUIRE(idsOf(model) == std::vector<std::string>{"x"});

    stack.redo();
    stack.redo();
    REQUIRE(idsOf(model) == std::vector<std::string>{"a", "b"});
    REQUIRE(noDuplicateIds(model));
}

TEST_CASE("an import command describes itself", "[app][AnnotationImportCommand]")
{
    AnnotationModel model;
    auto command = AnnotationImportCommand::create(model, {imported("a", 1.0)}, "Import 1 annotation");
    REQUIRE(command != nullptr);
    REQUIRE(command->description() == "Import 1 annotation");
}
