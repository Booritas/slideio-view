#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"

#include <QTemporaryDir>

#include <string>
#include <vector>

using slideio::viewer::infra::QSettingsAnnotationCategoryStore;

TEST_CASE("An unset category store answers the defaults", "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    const QSettingsAnnotationCategoryStore store(ini);

    REQUIRE(store.categories()
            == std::vector<std::string>{"Tumor", "Stroma", "Necrosis", "Normal", "Artifact"});
}

TEST_CASE("A stored category list survives a new store over the same file",
          "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    {
        QSettingsAnnotationCategoryStore store(ini);
        store.setCategories({"Mitosis", "Lymphocyte"});
    }

    // A second store over the same file: this is the across-sessions claim.
    const QSettingsAnnotationCategoryStore reopened(ini);

    REQUIRE(reopened.categories() == std::vector<std::string>{"Mitosis", "Lymphocyte"});
}

TEST_CASE("The store normalises what it is given", "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    QSettingsAnnotationCategoryStore store(ini);
    store.setCategories({"  Mitosis  ", "", "MITOSIS", "Lymphocyte"});

    REQUIRE(store.categories() == std::vector<std::string>{"Mitosis", "Lymphocyte"});
}

TEST_CASE("An explicitly empty category list is kept, not replaced by the defaults",
          "[infra][CategoryStore]")
{
    // A user who clears the list in Preferences meant to clear it. Answering
    // the defaults here would resurrect five categories they deleted.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    {
        QSettingsAnnotationCategoryStore store(ini);
        store.setCategories({});
    }

    const QSettingsAnnotationCategoryStore reopened(ini);

    REQUIRE(reopened.categories().empty());
}
