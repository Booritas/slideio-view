#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"

#include <QObject>

#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

std::string addRect(app::AnnotationModel& model, double x0, double y0, double x1, double y1)
{
    return model.add(core::AnnotationType::Rectangle, rect(x0, y0, x1, y1));
}

} // namespace

TEST_CASE("add stores an annotation and returns a non-empty id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE_FALSE(id.empty());
    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == id);
}

TEST_CASE("every added annotation gets a distinct id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string a = addRect(model, 0.0, 0.0, 10.0, 10.0);
    const std::string b = addRect(model, 0.0, 0.0, 10.0, 10.0);
    const std::string c = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
}

TEST_CASE("add emits annotationAdded with the new id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationAdded,
                     [&seen](const std::string& id) { seen.push_back(id); });

    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("remove deletes the annotation and emits annotationRemoved",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationRemoved,
                     [&seen](const std::string& removed) { seen.push_back(removed); });

    REQUIRE(model.remove(id));
    REQUIRE(model.annotations().empty());
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("remove reports false for an unknown id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    REQUIRE_FALSE(model.remove("not-a-real-id"));
}

TEST_CASE("setGeometry updates the shape and emits annotationChanged",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    int changes = 0;
    QObject::connect(&model, &app::AnnotationModel::annotationChanged,
                     [&changes](const std::string&) { ++changes; });

    REQUIRE(model.setGeometry(id, rect(5.0, 5.0, 25.0, 25.0)));
    REQUIRE(changes == 1);

    const core::Annotation* found = model.find(id);
    REQUIRE(found != nullptr);
    REQUIRE(found->boundingBox().x == 5.0);
    REQUIRE(found->boundingBox().width == 20.0);
}

TEST_CASE("hitTest returns the topmost annotation under the point",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 100.0, 100.0);
    const std::string top = addRect(model, 10.0, 10.0, 50.0, 50.0);

    // Both contain (20,20); the most recently added wins.
    REQUIRE(model.hitTest(core::PointF{20.0, 20.0}, 0.0) == top);
}

TEST_CASE("hitTest returns an empty id when nothing is under the point",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 10.0, 10.0);
    REQUIRE(model.hitTest(core::PointF{500.0, 500.0}, 0.0).empty());
}

TEST_CASE("setSelected records the selection and emits selectionChanged",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&seen](const std::string& selected) { seen.push_back(selected); });

    model.setSelected(id);
    REQUIRE(model.selectedId() == id);
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("selecting the already-selected annotation emits nothing",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    int emissions = 0;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&emissions](const std::string&) { ++emissions; });

    model.setSelected(id);
    REQUIRE(emissions == 0);
}

TEST_CASE("removing the selected annotation clears the selection",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    REQUIRE(model.remove(id));
    REQUIRE(model.selectedId().empty());
}

// Review Focus 3: annotations surviving a slide change would draw one slide's
// marks over another's tissue. Nothing is persisted in this slice, so a stale
// model is the only way that can happen -- and the only outcome is a clinical
// misassociation.
TEST_CASE("clear drops every annotation and the selection", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    addRect(model, 20.0, 20.0, 30.0, 30.0);
    model.setSelected(id);

    int clearedCount = 0;
    QObject::connect(&model, &app::AnnotationModel::cleared,
                     [&clearedCount]() { ++clearedCount; });

    model.clear();

    REQUIRE(model.annotations().empty());
    REQUIRE(model.selectedId().empty());
    REQUIRE(clearedCount == 1);
}

TEST_CASE("clear on an empty model still reports cleared", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    int clearedCount = 0;
    QObject::connect(&model, &app::AnnotationModel::cleared,
                     [&clearedCount]() { ++clearedCount; });

    model.clear();
    REQUIRE(clearedCount == 1);
}

TEST_CASE("remove emits the real id when passed the selection by reference",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationRemoved,
                     [&seen](const std::string& removed) { seen.push_back(removed); });

    // Aliases m_selectedId -- exactly what the Delete-key handler does.
    REQUIRE(model.remove(model.selectedId()));

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == id);
}

TEST_CASE("remove emits the real id when passed an element's own id by reference",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string first = addRect(model, 0.0, 0.0, 10.0, 10.0);
    addRect(model, 20.0, 20.0, 30.0, 30.0);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::annotationRemoved,
                     [&seen](const std::string& removed) { seen.push_back(removed); });

    // Aliases the first element's own m_id; erase() shifts the second down over it.
    REQUIRE(model.remove(model.annotations().front().id()));

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front() == first);
    REQUIRE(model.annotations().size() == 1);
}

TEST_CASE("find returns null for an unknown id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 10.0, 10.0);
    REQUIRE(model.find("not-a-real-id") == nullptr);
}

TEST_CASE("setGeometry on an unknown id reports false and emits nothing", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    addRect(model, 0.0, 0.0, 10.0, 10.0);

    int changes = 0;
    QObject::connect(&model, &app::AnnotationModel::annotationChanged,
                     [&changes](const std::string&) { ++changes; });

    REQUIRE_FALSE(model.setGeometry("not-a-real-id", rect(1.0, 1.0, 2.0, 2.0)));
    REQUIRE(changes == 0);
}

TEST_CASE("clearSelection empties the selection and emits selectionChanged with an empty id",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&seen](const std::string& selected) { seen.push_back(selected); });

    model.clearSelection();

    REQUIRE(model.selectedId().empty());
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().empty());
}

TEST_CASE("removing the selected annotation emits selectionChanged with an empty id",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&seen](const std::string& selected) { seen.push_back(selected); });

    REQUIRE(model.remove(id));

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().empty());
}

TEST_CASE("clear emits selectionChanged when a selection existed", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 10.0, 10.0);
    model.setSelected(id);

    std::vector<std::string> seen;
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     [&seen](const std::string& selected) { seen.push_back(selected); });

    model.clear();

    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().empty());
}
