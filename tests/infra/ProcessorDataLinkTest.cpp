#include <catch2/catch_test_macros.hpp>

#include <slideio/processor/config/configuration.hpp>
#include <slideio/processor/storage/store.hpp>
#include <slideio/processor/storage/types.hpp>

#include <QTemporaryDir>

#include <filesystem>

// Proves slideio-processor-data links and loads: a call into the library that
// needs no store on disk. Opening a store that does not exist read-only must
// throw rather than create it.
TEST_CASE("ProcessorDataLinkTest: a missing store fails to open read-only",
          "[infra][ProcessorDataLink]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const std::filesystem::path dir(temporary.path().toStdWString());

    REQUIRE_THROWS(slideio::processor::Store::open((dir / "workspace.store").string().c_str(),
                                                    slideio::processor::OpenMode::ReadOnly,
                                                    slideio::processor::Configuration()));
    REQUIRE_FALSE(std::filesystem::exists(dir / "workspace.store"));
}
