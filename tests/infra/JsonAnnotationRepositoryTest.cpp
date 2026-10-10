#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/JsonAnnotationRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <limits>
#include <string>

using namespace slideio::viewer::core;
using slideio::viewer::infra::JsonAnnotationRepository;

namespace
{

AnnotationDocument makeDocument(const std::string& slideId, int sceneIndex)
{
    AnnotationDocument document;
    document.slideId = slideId;
    document.sceneIndex = sceneIndex;
    document.slide.fileName = "case001_HE.svs";
    document.slide.path = "D:/data/case001_HE.svs";
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});
    return document;
}

} // namespace

TEST_CASE("the path carries both the slide id and the scene index",
          "[infra][JsonAnnotationRepository]")
{
    JsonAnnotationRepository repository("C:/workspace");

    const std::string scene0 = repository.pathFor({"a3f8c2e1b4d50697", 0});
    const std::string scene3 = repository.pathFor({"a3f8c2e1b4d50697", 3});

    REQUIRE(scene0.find("a3f8c2e1b4d50697.s0.annotations.json") != std::string::npos);
    REQUIRE(scene3.find("a3f8c2e1b4d50697.s3.annotations.json") != std::string::npos);
    REQUIRE(scene0 != scene3);
    REQUIRE(scene0.find("C:/workspace/annotations/") != std::string::npos);
}

TEST_CASE("an absent file is NotFound, not an error", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    const LoadResult result = repository.load({"nothing-here", 0});
    REQUIRE(result.status == LoadStatus::NotFound);
    REQUIRE_FALSE(result.path.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("a saved document loads back", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    const SaveResult saved = repository.save(makeDocument("a3f8c2e1b4d50697", 0));
    REQUIRE(saved.status == SaveStatus::Saved);

    const LoadResult loaded = repository.load({"a3f8c2e1b4d50697", 0});
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(loaded.document.slideId == "a3f8c2e1b4d50697");
    REQUIRE(loaded.document.annotations.size() == 1);
    REQUIRE(loaded.document.annotations.front().id() == "a1");
}

TEST_CASE("two scenes of one slide do not collide", "[infra][JsonAnnotationRepository]")
{
    // The whole reason the key carries a scene index.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    AnnotationDocument scene0 = makeDocument("same-slide", 0);
    scene0.annotations.front() = Annotation("from-scene-0", AnnotationType::Rectangle,
                                            RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});
    AnnotationDocument scene1 = makeDocument("same-slide", 1);
    scene1.annotations.front() = Annotation("from-scene-1", AnnotationType::Rectangle,
                                            RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    REQUIRE(repository.save(scene0).status == SaveStatus::Saved);
    REQUIRE(repository.save(scene1).status == SaveStatus::Saved);

    REQUIRE(repository.load({"same-slide", 0}).document.annotations.front().id() == "from-scene-0");
    REQUIRE(repository.load({"same-slide", 1}).document.annotations.front().id() == "from-scene-1");
}

TEST_CASE("the workspace directory is created on first save, not before",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString annotationsDir = dir.path() + "/annotations";

    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE_FALSE(QFileInfo::exists(annotationsDir));

    repository.load({"anything", 0});
    REQUIRE_FALSE(QFileInfo::exists(annotationsDir));

    REQUIRE(repository.save(makeDocument("anything", 0)).status == SaveStatus::Saved);
    REQUIRE(QFileInfo::exists(annotationsDir));
}

TEST_CASE("a save leaves no temporary file behind", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(repository.save(makeDocument("a3f8c2e1b4d50697", 0)).status == SaveStatus::Saved);

    const QDir annotations(dir.path() + "/annotations");
    const QStringList leftovers = annotations.entryList(QStringList() << "*.tmp" << "*~",
                                                        QDir::Files | QDir::Hidden);
    REQUIRE(leftovers.isEmpty());
    REQUIRE(annotations.entryList(QDir::Files).size() == 1);
}

TEST_CASE("a save overwrites the previous document rather than appending",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    REQUIRE(repository.save(makeDocument("slide", 0)).status == SaveStatus::Saved);

    AnnotationDocument emptied = makeDocument("slide", 0);
    emptied.annotations.clear();
    REQUIRE(repository.save(emptied).status == SaveStatus::Saved);

    const LoadResult loaded = repository.load({"slide", 0});
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(loaded.document.annotations.empty());
}

TEST_CASE("unparseable content on disk is Malformed and names the file",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(QDir().mkpath(dir.path() + "/annotations"));

    const QString path = QString::fromStdString(repository.pathFor({"broken", 0}));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json");
    file.close();

    const LoadResult result = repository.load({"broken", 0});
    REQUIRE(result.status == LoadStatus::Malformed);
    REQUIRE(result.path == path.toStdString());
    REQUIRE_FALSE(result.message.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("a future schema version is reported as such, not as malformed",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(QDir().mkpath(dir.path() + "/annotations"));

    QFile file(QString::fromStdString(repository.pathFor({"future", 0})));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(R"({"schemaVersion": 99, "slideId": "future", "sceneIndex": 0, "annotations": []})");
    file.close();

    REQUIRE(repository.load({"future", 0}).status == LoadStatus::UnsupportedVersion);
}

TEST_CASE("a document that cannot be serialized fails rather than writing a broken file",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    AnnotationDocument document = makeDocument("slide", 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    document.annotations.front().setGeometry(RectangleGeometry{PointF{nan, 0.0}, PointF{1.0, 1.0}});

    const SaveResult result = repository.save(document);
    REQUIRE(result.status == SaveStatus::Failed);
    REQUIRE_FALSE(QFileInfo::exists(QString::fromStdString(result.path)));
}

TEST_CASE("re-rooting sends later reads and writes to the new workspace",
          "[infra][JsonAnnotationRepository]")
{
    // The repository object has to survive a workspace change: the persistence
    // service holds a reference to it that is bound at construction.
    QTemporaryDir first;
    QTemporaryDir second;
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());

    JsonAnnotationRepository repository(first.path().toStdString());
    REQUIRE(repository.save(makeDocument("slide", 0)).status == SaveStatus::Saved);

    repository.setWorkspaceRoot(second.path().toStdString());
    REQUIRE(repository.workspaceRoot() == second.path().toStdString());

    // The old file is untouched and the new root starts empty -- changing the
    // setting must not move or lose anything already written.
    REQUIRE(QFileInfo::exists(first.path() + "/annotations/slide.s0.annotations.json"));
    REQUIRE(repository.load({"slide", 0}).status == LoadStatus::NotFound);

    JsonAnnotationRepository original(first.path().toStdString());
    const LoadResult old = original.load({"slide", 0});
    REQUIRE(old.status == LoadStatus::Loaded);
    REQUIRE(old.document.annotations.size() == 1);

    REQUIRE(repository.save(makeDocument("slide", 0)).status == SaveStatus::Saved);
    REQUIRE(QFileInfo::exists(second.path() + "/annotations/slide.s0.annotations.json"));
}

TEST_CASE("a workspace that cannot be created is NotWritable and names the path",
          "[infra][JsonAnnotationRepository]")
{
    // A file where the workspace directory should be: mkpath cannot succeed,
    // and the repository must say so rather than quietly writing elsewhere.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString blocker = dir.path() + "/blocked";
    QFile file(blocker);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("not a directory");
    file.close();

    JsonAnnotationRepository repository(blocker.toStdString());
    const SaveResult result = repository.save(makeDocument("slide", 0));

    REQUIRE(result.status == SaveStatus::NotWritable);
    REQUIRE(result.message.find("blocked") != std::string::npos);
}

TEST_CASE("a slide id that could escape the workspace is refused",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    const char* unsafe[] = {"../escape", "..", "a/b", "a\b", "C:evil", "has space", "pct%2fsep"};
    for (const char* id : unsafe) {
        REQUIRE(repository.load({id, 0}).status == LoadStatus::Unreadable);

        AnnotationDocument document = makeDocument(id, 0);
        REQUIRE(repository.save(document).status == SaveStatus::Failed);
    }

    // Nothing was created anywhere.
    REQUIRE_FALSE(QFileInfo::exists(dir.path() + "/annotations"));
}

TEST_CASE("a percent sequence in a slide id is not substituted into",
          "[infra][JsonAnnotationRepository]")
{
    // QString::arg chaining would have replaced "%2" with the scene index.
    JsonAnnotationRepository repository("C:/workspace");
    const std::string path = repository.pathFor({"abc%2def", 7});
    REQUIRE(path.find("abc%2def") != std::string::npos);
    REQUIRE(path.find(".s7.") != std::string::npos);
}

TEST_CASE("a negative scene index is refused", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(repository.save(makeDocument("a3f8c2e1b4d50697", -1)).status == SaveStatus::Failed);
}

TEST_CASE("a refused save leaves the previous file untouched",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    REQUIRE(repository.save(makeDocument("a3f8c2e1b4d50697", 0)).status == SaveStatus::Saved);

    AnnotationDocument broken = makeDocument("a3f8c2e1b4d50697", 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    broken.annotations.front().setGeometry(RectangleGeometry{PointF{nan, 0.0}, PointF{1.0, 1.0}});
    REQUIRE(repository.save(broken).status == SaveStatus::Failed);

    // The point: the good document is still there and still loads.
    const LoadResult reloaded = repository.load({"a3f8c2e1b4d50697", 0});
    REQUIRE(reloaded.status == LoadStatus::Loaded);
    REQUIRE(reloaded.document.annotations.size() == 1);
    REQUIRE(reloaded.document.annotations.front().id() == "a1");
}
