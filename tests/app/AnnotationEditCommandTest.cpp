#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationEditCommand.h"
#include "slideio/viewer/app/AnnotationModel.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

core::AnnotationGeometry polyline(int vertexCount)
{
    std::vector<core::PointF> vertices;
    vertices.reserve(static_cast<std::size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        vertices.push_back(core::PointF{static_cast<double>(i), static_cast<double>(i) * 2.0});
    }
    return core::PolygonGeometry{std::move(vertices), false};
}

/// The command a create push site builds: no before, the stored annotation after.
std::unique_ptr<app::AnnotationEditCommand> createCommand(app::AnnotationModel& model,
                                                          const std::string& id)
{
    const core::Annotation* stored = model.find(id);
    REQUIRE(stored != nullptr);
    return app::AnnotationEditCommand::create(model, id, std::nullopt, *stored,
                                              "Draw Rectangle");
}

} // namespace

TEST_CASE("undoing a create removes the annotation", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 10.0, 10.0));
    auto command = createCommand(model, id);

    command->undo();

    REQUIRE(model.annotations().empty());
    REQUIRE(model.find(id) == nullptr);
}

TEST_CASE("redoing a create restores it under the same id", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 10.0, 10.0));
    auto command = createCommand(model, id);

    command->undo();
    command->execute();

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("a create survives several undo/redo cycles without drifting",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(1.0, 2.0, 3.0, 4.0));
    auto command = createCommand(model, id);

    for (int i = 0; i < 5; ++i) {
        command->undo();
        command->execute();
    }

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("undoing a delete restores geometry and metadata exactly",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    model.setDefaultAuthor("booritas");
    const std::string id = model.add(rect(5.0, 6.0, 7.0, 8.0));
    const core::Annotation before = *model.find(id);

    auto command = app::AnnotationEditCommand::create(model, id, before, std::nullopt, "Delete Rectangle");
    REQUIRE(command != nullptr);
    command->execute(); // the delete itself
    REQUIRE(model.find(id) == nullptr);

    command->undo();

    const core::Annotation* restored = model.find(id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->metadata().author == "booritas");
    REQUIRE(restored->metadata().createdAt == before.metadata().createdAt);
    REQUIRE(restored->metadata().modifiedAt == before.metadata().modifiedAt);
    REQUIRE(std::get<core::RectangleGeometry>(restored->geometry()).topLeft.x == 5.0);
}

TEST_CASE("undoing a move returns the geometry vertex for vertex",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(polyline(32));
    const core::Annotation before = *model.find(id);

    core::Annotation after = before;
    after.setGeometry(core::translated(before.geometry(), 100.0, -50.0));
    model.setGeometry(id, after.geometry());

    auto command = app::AnnotationEditCommand::create(model, id, before, after, "Move Freehand Line");
    REQUIRE(command != nullptr);
    command->undo();

    const auto& restored = std::get<core::PolygonGeometry>(model.find(id)->geometry());
    const auto& original = std::get<core::PolygonGeometry>(before.geometry());
    REQUIRE(restored.vertices.size() == original.vertices.size());
    for (std::size_t i = 0; i < original.vertices.size(); ++i) {
        REQUIRE(restored.vertices[i].x == original.vertices[i].x);
        REQUIRE(restored.vertices[i].y == original.vertices[i].y);
    }
}

TEST_CASE("undoing a move keeps the annotation's place in paint order",
          "[app][AnnotationEditCommand]")
{
    // Paint order and hit-test order both follow this vector: painting walks it
    // forward, hitTest walks it in reverse. A large region drawn first sits
    // under a small focus drawn second. Undoing a move of the region must not
    // promote it over the focus.
    app::AnnotationModel model;
    const std::string region = model.add(rect(0.0, 0.0, 100.0, 100.0));
    const std::string focus = model.add(rect(10.0, 10.0, 20.0, 20.0));

    const core::Annotation before = *model.find(region);
    core::Annotation after = before;
    after.setGeometry(core::translated(before.geometry(), 5.0, 5.0));
    model.setGeometry(region, after.geometry());

    auto command = app::AnnotationEditCommand::create(model, region, before, after, "Move Rectangle");
    REQUIRE(command != nullptr);
    command->undo();

    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations()[0].id() == region);
    REQUIRE(model.annotations()[1].id() == focus);
    // The focus is still the one a click in the overlap finds.
    REQUIRE(model.hitTest(core::PointF{15.0, 15.0}, 0.0) == focus);
}

TEST_CASE("undoing a delete restores the annotation to its original place",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string first = model.add(rect(0.0, 0.0, 10.0, 10.0));
    const std::string second = model.add(rect(20.0, 20.0, 30.0, 30.0));
    const core::Annotation before = *model.find(first);

    auto command = app::AnnotationEditCommand::create(model, first, before, std::nullopt, "Delete Rectangle");
    REQUIRE(command != nullptr);
    command->execute();
    command->undo();

    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations()[0].id() == first);
    REQUIRE(model.annotations()[1].id() == second);
}

TEST_CASE("undo selects the annotation it restored", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation before = *model.find(id);

    auto command = app::AnnotationEditCommand::create(model, id, before, std::nullopt, "Delete Rectangle");
    REQUIRE(command != nullptr);
    command->execute();
    command->undo();

    REQUIRE(model.selectedId() == id);
}

TEST_CASE("undoing a create clears the selection", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    model.setSelected(id);
    auto command = createCommand(model, id);

    command->undo();

    REQUIRE(model.selectedId().empty());
}

TEST_CASE("undoing an edit whose annotation is already gone is harmless",
          "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation before = *model.find(id);
    core::Annotation after = before;
    after.setGeometry(rect(5.0, 5.0, 6.0, 6.0));

    auto command = app::AnnotationEditCommand::create(model, id, before, after, "Move Rectangle");
    REQUIRE(command != nullptr);
    model.remove(id); // some other path removed it

    command->undo(); // must not crash, must not double-insert

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("a command with neither state is not created", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;

    REQUIRE(app::AnnotationEditCommand::create(model, "some-id", std::nullopt, std::nullopt,
                                               "Nothing") == nullptr);
}

TEST_CASE("commands with either state are created", "[app][AnnotationEditCommand]")
{
    app::AnnotationModel model;
    const std::string id = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const core::Annotation stored = *model.find(id);

    REQUIRE(app::AnnotationEditCommand::create(model, id, std::nullopt, stored, "Draw") != nullptr);
    REQUIRE(app::AnnotationEditCommand::create(model, id, stored, std::nullopt, "Delete")
            != nullptr);
    REQUIRE(app::AnnotationEditCommand::create(model, id, stored, stored, "Move") != nullptr);
}

TEST_CASE("undoing a create leaves another annotation's selection alone",
          "[app][AnnotationEditCommand]")
{
    // Undoing one edit must not reach into an unrelated part of the user's
    // state. The annotation the command removes takes its own selection with
    // it; a selection on anything else is nobody else's to clear.
    app::AnnotationModel model;
    const std::string kept = model.add(rect(0.0, 0.0, 1.0, 1.0));
    const std::string created = model.add(rect(5.0, 5.0, 6.0, 6.0));
    auto command = createCommand(model, created);
    model.setSelected(kept);

    command->undo();

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.selectedId() == kept);
}
