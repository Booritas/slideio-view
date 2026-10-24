#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"
#include "slideio/viewer/app/WorkspaceSwitch.h"
#include "slideio/viewer/core/AnnotationRepository.h"

#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// Root-aware, unlike the FakeRepository in AnnotationPersistenceServiceTest.cpp:
/// a save made under one root must be invisible to a load from another, which is
/// the whole point of what is under test here.
class FakeRootRepository : public core::IAnnotationRepository
{
public:
    std::string root = "/old";
    /// Set to anything but Loaded to make loads from `forcedForRoot` fail.
    core::LoadStatus forcedStatus = core::LoadStatus::Loaded;
    std::string forcedForRoot;
    core::SaveStatus nextSave = core::SaveStatus::Saved;

    std::map<std::pair<std::string, std::string>, core::AnnotationDocument> stored;
    std::vector<std::pair<std::string, std::string>> saves; ///< (root, path), in order
    std::vector<std::pair<std::string, std::string>> loads; ///< (root, path), in order

    core::LoadResult load(const core::AnnotationKey& key) override
    {
        loads.emplace_back(root, pathFor(key));
        core::LoadResult result;
        result.path = pathFor(key);
        if (!forcedForRoot.empty() && forcedForRoot == root) {
            result.status = forcedStatus;
            result.message = "forced";
            return result;
        }
        const auto found = stored.find({root, keyText(key)});
        if (found == stored.end()) {
            result.status = core::LoadStatus::NotFound;
            return result;
        }
        result.status = core::LoadStatus::Loaded;
        result.document = found->second;
        return result;
    }

    core::SaveResult save(const core::AnnotationDocument& document) override
    {
        const core::AnnotationKey key{document.slideId, document.sceneIndex};
        saves.emplace_back(root, pathFor(key));
        core::SaveResult result;
        result.status = nextSave;
        result.path = pathFor(key);
        if (nextSave == core::SaveStatus::Saved) {
            stored[{root, keyText(key)}] = document;
        }
        return result;
    }

    /// The switch is about roots, not about contention, so this fake always
    /// grants the lock and only counts the calls -- which is enough to assert
    /// that a re-root releases before it re-acquires.
    core::LockResult lock(const core::AnnotationKey& key) override
    {
        lockCount++;
        core::LockResult result;
        result.status = core::LockStatus::Acquired;
        result.path = pathFor(key) + ".lock";
        return result;
    }

    void unlock() override
    {
        unlockCount++;
    }

    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override
    {
        return root + "/annotations/" + keyText(key) + ".annotations.json";
    }

    int lockCount = 0;
    int unlockCount = 0;

    [[nodiscard]] static std::string keyText(const core::AnnotationKey& key)
    {
        return key.slideId + ".s" + std::to_string(key.sceneIndex);
    }
};

core::SlideProvenance provenance()
{
    core::SlideProvenance p;
    p.fileName = "slide.svs";
    p.path = "/slides/slide.svs";
    return p;
}

core::Annotation annotation(const std::string& id, int64_t modifiedSeconds)
{
    core::AnnotationProperties properties;
    core::AnnotationMetadata metadata;
    metadata.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(modifiedSeconds));
    metadata.modifiedAt = metadata.createdAt;
    return core::Annotation(id, core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}},
                            properties, metadata);
}

const core::AnnotationKey kKey{"slide-1", 0};

/// A switch harness wired the way MainWindow wires it.
struct Harness
{
    FakeRootRepository repository;
    app::AnnotationModel model;
    app::AnnotationPersistenceService persistence{repository, model};
    app::WorkspaceSwitch sut{repository, model, persistence,
                             [this]() { return repository.root; },
                             [this](std::string root) { repository.root = std::move(root); }};

    void openWith(std::vector<core::Annotation> annotations)
    {
        core::AnnotationDocument document;
        document.slideId = kKey.slideId;
        document.sceneIndex = kKey.sceneIndex;
        document.annotations = std::move(annotations);
        repository.stored[{"/old", FakeRootRepository::keyText(kKey)}] = document;
        persistence.beginSlide(kKey, provenance());
    }
};

} // namespace

TEST_CASE("the flush reaches the old root before the probe reaches the new one",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::LoadNew;
    });

    h.sut.run("/new", kKey, provenance());

    REQUIRE_FALSE(h.repository.saves.empty());
    REQUIRE(h.repository.saves.front().first == "/old");
    REQUIRE_FALSE(h.repository.loads.empty());
    REQUIRE(h.repository.loads.back().first == "/new");
}

TEST_CASE("copy writes the on-screen set to the new root", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(options.copy);
        REQUIRE(facts.currentCount == 2);
        REQUIRE_FALSE(facts.newFileExists);
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::Saved);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 1);
    REQUIRE(h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}].annotations.size() == 2);
    REQUIRE(h.model.annotations().size() == 2);
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("merge writes the merged document and reports the counts",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});

    core::AnnotationDocument inNew;
    inNew.slideId = kKey.slideId;
    inNew.annotations = {annotation("b", 300), annotation("c", 100)};
    h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}] = inNew;

    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(options.merge);
        REQUIRE(facts.newCount == 2);
        return app::WorkspaceChoice::Merge;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.merge.adopted == 1);
    REQUIRE(result.merge.superseded == 1);
    REQUIRE(result.merge.kept == 0);
    REQUIRE(h.model.annotations().size() == 3);
}

TEST_CASE("use-the-new-folder's writes nothing", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::LoadNew;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.model.annotations().empty());
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("keep-current writes the on-screen set over the new folder's file",
          "[app][WorkspaceSwitch]")
{
    // The one choice that deliberately destroys a file, and so the one whose
    // failure costs another reader their annotations.
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});

    core::AnnotationDocument inNew;
    inNew.slideId = kKey.slideId;
    inNew.annotations = {annotation("z", 500)};
    h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}] = inNew;

    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(options.keepCurrent);
        REQUIRE(facts.newCount == 1);
        return app::WorkspaceChoice::KeepCurrent;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::Saved);
    const core::AnnotationDocument& written =
        h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}];
    REQUIRE(written.annotations.size() == 2);
    REQUIRE(written.annotations[0].id() == "a");
    REQUIRE(written.annotations[1].id() == "b");
    REQUIRE(h.model.annotations().size() == 2);
}

TEST_CASE("a write that fails keeps the annotations in the model and dirty",
          "[app][WorkspaceSwitch]")
{
    // beginSlide reloads from the new folder's file. When the write never
    // reached that file, the file is not the truth, and letting it win would
    // drop the user's work on the floor with only a transient message.
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});
    h.repository.nextSave = core::SaveStatus::NotWritable;
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::NotWritable);
    REQUIRE(h.model.annotations().size() == 2);
    // Dirty, so the ordinary autosave machinery -- with its retries and its
    // save-failure resolver -- owns the retry instead of this one-shot save.
    REQUIRE(h.persistence.isDirty());
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("cancel after a failed flush keeps the on-screen set",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    // One annotation on disk, two on screen, and the flush that would have
    // reconciled them fails.
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.repository.nextSave = core::SaveStatus::NotWritable;
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Cancel;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.choice == app::WorkspaceChoice::Cancel);
    REQUIRE(h.repository.root == "/old");
    // Without the restore, beginSlide reloads the one-annotation file and the
    // second annotation is gone -- from a dialog the user cancelled.
    REQUIRE(h.model.annotations().size() == 2);
    REQUIRE(h.persistence.isDirty());
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("cancel re-points back and leaves the slide saving", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Cancel;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.choice == app::WorkspaceChoice::Cancel);
    REQUIRE(h.repository.root == "/old");
    REQUIRE(h.model.annotations().size() == 2);
    // The failure this catches is invisible until the next edit vanishes:
    // step 1's endSlide() deactivated the service, and only a beginSlide
    // revives it.
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("an unparseable file in the new root is never written over",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.repository.forcedForRoot = "/new";
    h.repository.forcedStatus = core::LoadStatus::Malformed;

    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts& facts,
                               const core::WorkspaceSwitchOptions& options) {
        asked = true;
        REQUIRE_FALSE(facts.newFileReadable);
        REQUIRE_FALSE(options.merge);
        REQUIRE_FALSE(options.keepCurrent);
        REQUIRE_FALSE(options.copy);
        // A resolver returning an option that was not offered is a ui bug, and
        // acting on it would write over a file nobody could read.
        return app::WorkspaceChoice::Merge;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(asked);
    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
}

TEST_CASE("a failed flush still allows the copy that rescues the work",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.repository.nextSave = core::SaveStatus::NotWritable;

    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(facts.flushFailed);
        REQUIRE(options.copy);
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    // The write was attempted against the new root and reported honestly.
    REQUIRE(result.write == core::SaveStatus::NotWritable);
    REQUIRE(h.repository.saves.back().first == "/new");
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("a rescued flush abandons the switch", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.repository.nextSave = core::SaveStatus::NotWritable;

    // Stands in for ui's save-failure dialog, which can re-point the workspace
    // to a folder the user picks while the failure is on screen.
    h.persistence.setSaveFailureResolver([&h](const core::SaveResult&, int) {
        h.repository.root = "/rescue";
        h.repository.nextSave = core::SaveStatus::Saved;
        return app::AnnotationPersistenceService::SaveFailureChoice::Retry;
    });

    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        asked = true;
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.rescued);
    REQUIRE_FALSE(asked);
    REQUIRE(h.repository.root == "/rescue");
    // Nothing was probed or written in the folder the dialog asked for.
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("no dialog when nothing is on screen", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({});
    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        asked = true;
        return app::WorkspaceChoice::Cancel;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE_FALSE(asked);
    REQUIRE_FALSE(result.asked);
    REQUIRE(h.repository.root == "/new");
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("no resolver writes nothing and still leaves the slide saving",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("result.asked is false when there is no resolver to ask with",
          "[app][WorkspaceSwitch]")
{
    // `asked` is read by MainWindow to decide how to report the switch. With
    // no resolver set, run() chooses LoadNew on its own, and reporting that as
    // a choice the user was offered would describe something that did not
    // happen. The options still say an ask was warranted -- that is the point:
    // the two are not the same fact.
    Harness h;
    h.openWith({annotation("a", 100)});
    h.repository.forcedForRoot = "/new";
    h.repository.forcedStatus = core::LoadStatus::Malformed;

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE_FALSE(result.asked);
    REQUIRE(result.choice == app::WorkspaceChoice::LoadNew);
}

namespace
{

/// Teaching hidden and a custom layer on top: a list a write without layers
/// would silently reset to the defaults, revealing the hidden layer.
std::vector<core::AnnotationLayer> customLayers()
{
    std::vector<core::AnnotationLayer> layers = core::defaultLayers();
    layers[1].visible = false;
    layers.push_back(core::AnnotationLayer{"students", "Students"});
    return layers;
}

core::Annotation onLayer(const std::string& id, const std::string& layerId)
{
    core::Annotation a = annotation(id, 100);
    a.setLayerId(layerId);
    return a;
}

} // namespace

TEST_CASE("copy writes the scene's layers with its annotations", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({onLayer("a", "students")});
    REQUIRE(h.model.setLayers(customLayers()));
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Copy;
    });

    h.sut.run("/new", kKey, provenance());

    CHECK(h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}].layers == customLayers());
    CHECK(h.model.layers() == customLayers());
}

TEST_CASE("merge keeps both folders' layers", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    REQUIRE(h.model.setLayers(customLayers()));

    core::AnnotationDocument inNew;
    inNew.slideId = kKey.slideId;
    inNew.layers = core::defaultLayers();
    inNew.layers.push_back(core::AnnotationLayer{"theirs", "Theirs"});
    inNew.annotations = {onLayer("c", "theirs")};
    h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}] = inNew;
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Merge;
    });

    h.sut.run("/new", kKey, provenance());

    const std::vector<core::AnnotationLayer> expected = core::mergeLayerLists(customLayers(), inNew.layers);
    CHECK(h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}].layers == expected);
    CHECK(h.model.layers() == expected);
    CHECK(core::findLayer(h.model.layers(), "theirs") != nullptr);
}

TEST_CASE("a write that fails keeps the layers in the model too", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({onLayer("a", "students")});
    REQUIRE(h.model.setLayers(customLayers()));
    h.repository.nextSave = core::SaveStatus::NotWritable;
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Copy;
    });

    h.sut.run("/new", kKey, provenance());

    CHECK(h.model.layers() == customLayers());
    CHECK(h.persistence.isDirty());
}

TEST_CASE("cancel after a failed flush keeps the layers on screen", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.repository.nextSave = core::SaveStatus::NotWritable;
    REQUIRE(h.model.setLayers(customLayers()));
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Cancel;
    });

    h.sut.run("/new", kKey, provenance());

    CHECK(h.model.layers() == customLayers());
    CHECK(h.persistence.isDirty());
}
