#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"

#include <QObject>

#include <chrono>
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

TEST_CASE("insert keeps the id the annotation already carries", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    model.insert(core::Annotation("stored-id-42", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{10.0, 10.0}}));

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == "stored-id-42");
    REQUIRE(model.find("stored-id-42") != nullptr);
}

TEST_CASE("insert preserves stored metadata rather than restamping it",
          "[app][AnnotationModel]")
{
    core::AnnotationMetadata metadata;
    metadata.author = "a.colleague";
    metadata.createdAt = std::chrono::system_clock::from_time_t(1000000);
    metadata.modifiedAt = std::chrono::system_clock::from_time_t(2000000);

    app::AnnotationModel model;
    model.setDefaultAuthor("me");
    model.insert(core::Annotation("a1", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{1.0, 1.0}},
                                  core::AnnotationProperties{}, metadata));

    const core::Annotation* loaded = model.find("a1");
    REQUIRE(loaded != nullptr);
    // An annotation made by a colleague keeps their name when opened here.
    REQUIRE(loaded->metadata().author == "a.colleague");
    REQUIRE(loaded->metadata().createdAt == metadata.createdAt);
}

TEST_CASE("insert emits annotationAdded", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    int added = 0;
    QObject::connect(&model, &app::AnnotationModel::annotationAdded,
                     &model, [&added](const std::string&) { ++added; });

    model.insert(core::Annotation("a1", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{1.0, 1.0}}));
    REQUIRE(added == 1);
}

TEST_CASE("replaceAll swaps the contents and emits one modelReset",
          "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int resets = 0;
    int adds = 0;
    QObject::connect(&model, &app::AnnotationModel::modelReset, &model, [&resets]() { ++resets; });
    QObject::connect(&model, &app::AnnotationModel::annotationAdded,
                     &model, [&adds](const std::string&) { ++adds; });

    std::vector<core::Annotation> loaded;
    loaded.emplace_back("x1", core::AnnotationType::Rectangle,
                        core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0, 2.0}});
    loaded.emplace_back("x2", core::AnnotationType::Rectangle,
                        core::RectangleGeometry{core::PointF{3.0, 3.0}, core::PointF{4.0, 4.0}});
    model.replaceAll(std::move(loaded));

    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations().front().id() == "x1");
    // One signal for the whole load, not one per annotation.
    REQUIRE(resets == 1);
    REQUIRE(adds == 0);
}

TEST_CASE("replaceAll clears the selection", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    model.setSelected(id);
    REQUIRE_FALSE(model.selectedId().empty());

    model.replaceAll({});
    REQUIRE(model.selectedId().empty());
    REQUIRE(model.annotations().empty());
}

TEST_CASE("replaceAll with an empty vector still emits modelReset",
          "[app][AnnotationModel]")
{
    // Loading a slide with no annotation file must still tell listeners to
    // repaint, or the previous slide's rectangles stay on screen.
    app::AnnotationModel model;
    int resets = 0;
    QObject::connect(&model, &app::AnnotationModel::modelReset, &model, [&resets]() { ++resets; });

    model.replaceAll({});
    REQUIRE(resets == 1);
}

TEST_CASE("add stamps the configured default author", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    model.setDefaultAuthor("s.melnikov");
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});

    const core::Annotation* created = model.find(id);
    REQUIRE(created != nullptr);
    REQUIRE(created->metadata().author == "s.melnikov");
    // The timestamps are still stamped at creation.
    REQUIRE(created->metadata().createdAt.time_since_epoch().count() != 0);
}

TEST_CASE("add with no configured author leaves it empty", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    REQUIRE(model.find(id)->metadata().author.empty());
}

TEST_CASE("insert rejects an annotation with an empty id", "[app][AnnotationModel]")
{
    // The parser refuses an empty id, so storing one would write a file that cannot be read back.
    app::AnnotationModel model;
    int added = 0;
    QObject::connect(&model, &app::AnnotationModel::annotationAdded,
                     &model, [&added](const std::string&) { ++added; });

    model.insert(core::Annotation("", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{1.0, 1.0}}));
    REQUIRE(model.annotations().empty());
    REQUIRE(added == 0);
}

TEST_CASE("replaceAll emits modelReset before selectionChanged", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    const std::string id = addRect(model, 0.0, 0.0, 1.0, 1.0);
    model.setSelected(id);

    std::vector<std::string> order;
    QObject::connect(&model, &app::AnnotationModel::modelReset, &model, [&order]() { order.push_back("reset"); });
    QObject::connect(&model, &app::AnnotationModel::selectionChanged,
                     &model, [&order](const std::string&) { order.push_back("selection"); });

    model.replaceAll({});
    REQUIRE(order == std::vector<std::string>{"reset", "selection"});
}

TEST_CASE("replaceAll skips annotations with an empty id", "[app][AnnotationModel]")
{
    app::AnnotationModel model;
    std::vector<core::Annotation> loaded;
    loaded.emplace_back("", core::AnnotationType::Rectangle, rect(0.0, 0.0, 1.0, 1.0));
    loaded.emplace_back("ok", core::AnnotationType::Rectangle, rect(2.0, 2.0, 3.0, 3.0));
    model.replaceAll(std::move(loaded));

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == "ok");
}
