#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include <map>
#include <string>
#include <vector>

using namespace slideio::viewer;
using app::AnnotationModel;
using app::AnnotationPersistenceService;

namespace
{

/// Records every call so the tests can assert on what the service did, and
/// never touches the filesystem.
class FakeRepository : public core::IAnnotationRepository
{
public:
    core::LoadResult load(const core::AnnotationKey& key) override
    {
        loadCount++;
        lastLoadedKey = key;
        return nextLoad;
    }

    core::SaveResult save(const core::AnnotationDocument& document) override
    {
        saveCount++;
        lastSaved = document;
        return nextSave;
    }

    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override
    {
        return "/fake/" + key.slideId + ".s" + std::to_string(key.sceneIndex) + ".json";
    }

    int loadCount = 0;
    int saveCount = 0;
    core::AnnotationKey lastLoadedKey;
    core::AnnotationDocument lastSaved;
    core::LoadResult nextLoad;
    core::SaveResult nextSave{core::SaveStatus::Saved, {}, "/fake/path"};
};

core::SlideProvenance provenance()
{
    core::SlideProvenance slide;
    slide.fileName = "case001_HE.svs";
    slide.path = "D:/data/case001_HE.svs";
    slide.width = 102400;
    slide.height = 76800;
    return slide;
}

core::Annotation rectangle(const std::string& id)
{
    return core::Annotation(id, core::AnnotationType::Rectangle,
                            core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
}

core::LoadResult loadedWith(const std::string& slideId, int sceneIndex,
                            std::vector<core::Annotation> annotations)
{
    core::LoadResult result;
    result.status = core::LoadStatus::Loaded;
    result.path = "/fake/path";
    result.document.slideId = slideId;
    result.document.sceneIndex = sceneIndex;
    result.document.annotations = std::move(annotations);
    return result;
}

} // namespace

TEST_CASE("beginSlide loads the key's annotations into the model",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1"), rectangle("a2")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    REQUIRE(repository.loadCount == 1);
    REQUIRE(repository.lastLoadedKey.slideId == "slide-1");
    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations().front().id() == "a1");
    REQUIRE(service.isActive());
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("a slide with no annotation file is active with an empty model",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::NotFound;

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"fresh-slide", 0}, provenance());

    REQUIRE(service.isActive());
    REQUIRE(model.annotations().empty());
}

TEST_CASE("loading does not mark the model dirty", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    REQUIRE_FALSE(service.isDirty());
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a mutation marks the model dirty", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    SECTION("adding")
    {
        model.add(core::AnnotationType::Rectangle,
                  core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
        REQUIRE(service.isDirty());
    }

    SECTION("moving")
    {
        const std::string id = model.add(core::AnnotationType::Rectangle,
                                         core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                                 core::PointF{1.0, 1.0}});
        service.flush();
        REQUIRE_FALSE(service.isDirty());
        model.setGeometry(id, core::RectangleGeometry{core::PointF{5.0, 5.0},
                                                      core::PointF{6.0, 6.0}});
        REQUIRE(service.isDirty());
    }

    SECTION("removing")
    {
        const std::string id = model.add(core::AnnotationType::Rectangle,
                                         core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                                 core::PointF{1.0, 1.0}});
        service.flush();
        model.remove(id);
        REQUIRE(service.isDirty());
    }
}

TEST_CASE("changing the selection does not mark the model dirty",
          "[app][AnnotationPersistenceService]")
{
    // Clicking an annotation must not trigger a save.
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE_FALSE(service.isDirty());

    model.setSelected(id);
    model.clearSelection();
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("flush writes the model and clears the dirty flag",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-7", 2}, provenance());

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.flush().status == core::SaveStatus::Saved);

    REQUIRE(repository.saveCount == 1);
    REQUIRE(repository.lastSaved.slideId == "slide-7");
    REQUIRE(repository.lastSaved.sceneIndex == 2);
    REQUIRE(repository.lastSaved.slide.fileName == "case001_HE.svs");
    REQUIRE(repository.lastSaved.annotations.size() == 1);
    REQUIRE(repository.lastSaved.schemaVersion == core::kCurrentSchemaVersion);
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("a clean model is not written", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    service.flush();
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("deleting the last annotation still writes an empty document",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    model.remove("a1");
    REQUIRE(service.flush().status == core::SaveStatus::Saved);
    REQUIRE(repository.saveCount == 1);
    REQUIRE(repository.lastSaved.annotations.empty());
}

TEST_CASE("endSlide flushes and then goes inactive",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.endSlide();

    REQUIRE(repository.saveCount == 1);
    REQUIRE_FALSE(service.isActive());

    // A mutation after endSlide belongs to nobody and must not be written.
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{2.0, 2.0}, core::PointF{3.0, 3.0}});
    service.flush();
    REQUIRE(repository.saveCount == 1);
}

TEST_CASE("endSlide with no slide is safe", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    REQUIRE_FALSE(service.isActive());
    service.endSlide();
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("an empty slide id never becomes active", "[app][AnnotationPersistenceService]")
{
    // computeSlideId returns the same value for every unreadable file, so a
    // caller that cannot identify a slide must not look anything up.
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    REQUIRE(repository.loadCount == 0);

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a malformed file leaves the service inactive and never saves over it",
          "[app][AnnotationPersistenceService]")
{
    // The single worst outcome this subsystem can produce is overwriting the
    // user's only copy with an empty document because we could not read theirs.
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::Malformed;
    repository.nextLoad.path = "/fake/broken.json";
    repository.nextLoad.message = "the file is not valid JSON";

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    QString failedPath;
    QString failedMessage;
    QObject::connect(&service, &AnnotationPersistenceService::loadFailed, &service,
                     [&](const QString& path, const QString& message) {
                         failedPath = path;
                         failedMessage = message;
                     });

    service.beginSlide({"broken-slide", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    REQUIRE(failedPath == "/fake/broken.json");
    REQUIRE(failedMessage.contains("valid JSON"));

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a future schema version also blocks saving",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::UnsupportedVersion;

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"future-slide", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a slide id mismatch is referred to the resolver, never resolved silently",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    // A file hand-copied from another slide: the name matched, the content does not.
    repository.nextLoad = loadedWith("a-different-slide", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    int resolverCalls = 0;

    SECTION("ignore")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::Ignore;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(resolverCalls == 1);
        REQUIRE(model.annotations().empty());
        REQUIRE_FALSE(service.isActive());
        REQUIRE_FALSE(service.isDirty());
        REQUIRE(service.flush().status == core::SaveStatus::NothingToDo);
        REQUIRE(repository.saveCount == 0);
    }

    SECTION("read-only loads but never saves")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::ReadOnly;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(model.annotations().size() == 1);
        REQUIRE_FALSE(service.isActive());
        REQUIRE_FALSE(service.isDirty());
        model.remove("a1");
        service.flush();
        REQUIRE(repository.saveCount == 0);
    }

    SECTION("re-associate adopts the open slide's id")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::Reassociate;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(model.annotations().size() == 1);
        REQUIRE(service.isActive());

        model.add(core::AnnotationType::Rectangle,
                  core::RectangleGeometry{core::PointF{9.0, 9.0}, core::PointF{10.0, 10.0}});
        REQUIRE(service.flush().status == core::SaveStatus::Saved);
        REQUIRE(repository.lastSaved.slideId == "the-open-slide");
    }
}

TEST_CASE("re-association is written even when the user draws nothing",
          "[app][AnnotationPersistenceService]")
{
    // Otherwise the mismatch dialog returns on every single open.
    FakeRepository repository;
    repository.nextLoad = loadedWith("a-different-slide", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.setMismatchResolver([](const core::AnnotationDocument&, const core::SlideProvenance&) {
        return AnnotationPersistenceService::MismatchChoice::Reassociate;
    });
    service.beginSlide({"the-open-slide", 0}, provenance());

    REQUIRE(service.isActive());
    REQUIRE(service.isDirty());

    REQUIRE(service.flush().status == core::SaveStatus::Saved);
    REQUIRE(repository.saveCount == 1);
    REQUIRE(repository.lastSaved.slideId == "the-open-slide");
    REQUIRE(repository.lastSaved.annotations.size() == 1);
}

TEST_CASE("with no resolver installed a mismatch is treated as read-only",
          "[app][AnnotationPersistenceService]")
{
    // The safe default: show the annotations, refuse to write.
    FakeRepository repository;
    repository.nextLoad = loadedWith("a-different-slide", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"the-open-slide", 0}, provenance());

    REQUIRE(model.annotations().size() == 1);
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("a failed save is reported and leaves the model dirty",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable,
                                           "the folder is read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    QString reportedPath;
    QObject::connect(&service, &AnnotationPersistenceService::saveFailed, &service,
                     [&](const QString& path, const QString&) { reportedPath = path; });

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.flush().status == core::SaveStatus::NotWritable);

    REQUIRE(reportedPath == "/fake/path");
    // Still dirty, so the next autosave tick retries rather than forgetting.
    REQUIRE(service.isDirty());
}

TEST_CASE("a save that fails while closing asks the resolver",
          "[app][AnnotationPersistenceService]")
{
    // openScene() calls closeSlide() internally, so this is the slide-switch
    // path. Failing silently here is how an hour of work disappears.
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable,
                                           "the folder is read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    int reportedCount = -1;
    service.setSaveFailureResolver(
        [&](const core::SaveResult& failure, int count) {
            ++asked;
            reportedCount = count;
            REQUIRE(failure.status == core::SaveStatus::NotWritable);
            return AnnotationPersistenceService::SaveFailureChoice::Discard;
        });

    service.endSlide();

    REQUIRE(asked == 1);
    // The dialog names how many annotations are at stake, so it must be told.
    REQUIRE(reportedCount == 1);
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("saveFailed is silent during endSlide with a resolver but still fires from flush",
          "[app][AnnotationPersistenceService]")
{
    // The resolver dialog and saveAbandoned already tell the user about a
    // close-time failure; a second "will be retried" modal contradicts them.
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable,
                                           "the folder is read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int failedSignals = 0;
    QObject::connect(&service, &AnnotationPersistenceService::saveFailed, &service,
                     [&](const QString&, const QString&) { ++failedSignals; });

    // An ordinary failing flush still reports.
    REQUIRE(service.flush().status == core::SaveStatus::NotWritable);
    REQUIRE(failedSignals == 1);

    service.setSaveFailureResolver([](const core::SaveResult&, int) {
        return AnnotationPersistenceService::SaveFailureChoice::Discard;
    });
    service.endSlide();

    REQUIRE(failedSignals == 1);
}

TEST_CASE("Retry re-attempts the save and succeeds once the cause is fixed",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable, "read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    service.setSaveFailureResolver(
        [&](const core::SaveResult&, int) {
            ++asked;
            // Standing in for the user choosing a writable folder.
            repository.nextSave = core::SaveResult{core::SaveStatus::Saved, {}, "/fake/path"};
            return AnnotationPersistenceService::SaveFailureChoice::Retry;
        });

    service.endSlide();

    REQUIRE(asked == 1);
    REQUIRE(repository.saveCount == 2);
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("with no resolver a failing close still completes",
          "[app][AnnotationPersistenceService]")
{
    // Better to lose the annotations than to leave the application unable to
    // close a slide. The saveFailed signal still reports it.
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::Failed, "disk full", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    service.endSlide();
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("a successful close never asks the resolver",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    service.setSaveFailureResolver([&](const core::SaveResult&, int) {
        ++asked;
        return AnnotationPersistenceService::SaveFailureChoice::Discard;
    });

    service.endSlide();
    REQUIRE(asked == 0);
}

TEST_CASE("activeChanged reports both directions", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    std::vector<bool> states;
    QObject::connect(&service, &AnnotationPersistenceService::activeChanged, &service,
                     [&states](bool active) { states.push_back(active); });

    service.beginSlide({"slide-1", 0}, provenance());
    service.endSlide();

    REQUIRE(states == std::vector<bool>{true, false});
}

TEST_CASE("a scene index mismatch is referred to the resolver like a slide id mismatch",
          "[app][AnnotationPersistenceService]")
{
    // The parser defaults an absent sceneIndex to 0, so a scene-0 file could
    // otherwise be accepted for scene 3 with a matching slide id.
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    int resolverCalls = 0;
    service.setMismatchResolver([&](const core::AnnotationDocument&, const core::SlideProvenance&) {
        ++resolverCalls;
        return AnnotationPersistenceService::MismatchChoice::ReadOnly;
    });
    service.beginSlide({"slide-1", 3}, provenance());

    REQUIRE(resolverCalls == 1);
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("clearing the model under an active service stops it writing",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.isDirty());

    model.clear();

    REQUIRE_FALSE(service.isActive());
    REQUIRE_FALSE(service.isDirty());
    service.flush();
    REQUIRE(repository.saveCount == 0); // never an empty document over the file
}

TEST_CASE("a failed save stays dirty and a flush with nothing to do says so",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::Failed, "disk full", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    int failures = 0;
    QObject::connect(&service, &AnnotationPersistenceService::saveFailed, &service,
                     [&failures](const QString&, const QString&) { ++failures; });

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.flush().status == core::SaveStatus::Failed);
    // An explicit flush always attempts the write; the debounce only paces the timer.
    REQUIRE(service.flush().status == core::SaveStatus::Failed);
    REQUIRE(failures == 2);
    REQUIRE(service.isDirty());

    repository.nextSave = core::SaveResult{core::SaveStatus::Saved, {}, "/fake/path"};
    REQUIRE(service.flush().status == core::SaveStatus::Saved);
    REQUIRE(service.flush().status == core::SaveStatus::NothingToDo);
}

TEST_CASE("flush on an inactive service reports NothingToDo", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    REQUIRE(service.flush().status == core::SaveStatus::NothingToDo);
}

TEST_CASE("a resolver that always retries cannot wedge the close",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::Failed, "disk full", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    service.setSaveFailureResolver([&](const core::SaveResult&, int) {
        ++asked;
        return AnnotationPersistenceService::SaveFailureChoice::Retry;
    });
    service.endSlide();

    REQUIRE(asked == 5);
    REQUIRE_FALSE(service.isActive());
}
