#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ColorProfileOverride.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

// A store with no QSettings behind it, so core keeps its own tests Qt-free.
class FakeOverrideStore : public IColorProfileOverrideStore
{
public:
    std::optional<ColorProfileOverride> find(const std::string& slideId) const override
    {
        const auto it = m_entries.find(slideId);
        if (it == m_entries.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void set(const ColorProfileOverride& entry) override
    {
        m_entries[entry.slideId] = entry;
    }

    void remove(const std::string& slideId) override
    {
        m_entries.erase(slideId);
    }

    std::vector<ColorProfileOverride> all() const override
    {
        std::vector<ColorProfileOverride> out;
        out.reserve(m_entries.size());
        for (const auto& pair : m_entries) {
            out.push_back(pair.second);
        }
        return out;
    }

private:
    std::unordered_map<std::string, ColorProfileOverride> m_entries;
};

ColorProfileOverride makeEntry(const std::string& slideId, const std::string& profilePath)
{
    ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = profilePath;
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;
    return entry;
}

} // namespace

TEST_CASE("store round-trips an entry", "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    store.set(makeEntry("a3f8c2e109b74d21", "C:/profiles/x.icc"));

    const auto found = store.find("a3f8c2e109b74d21");
    REQUIRE(found.has_value());
    REQUIRE(found->profilePath == "C:/profiles/x.icc");
    REQUIRE(found->slideDisplayName == "case001_HE.svs");
    REQUIRE(found->displacedEmbedded);
}

TEST_CASE("store reports a miss as empty, not as a default entry",
          "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    REQUIRE_FALSE(store.find("0000000000000000").has_value());
}

TEST_CASE("store removes one entry and leaves the rest",
          "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    store.set(makeEntry("aaaaaaaaaaaaaaaa", "C:/profiles/a.icc"));
    store.set(makeEntry("bbbbbbbbbbbbbbbb", "C:/profiles/b.icc"));

    store.remove("aaaaaaaaaaaaaaaa");

    REQUIRE_FALSE(store.find("aaaaaaaaaaaaaaaa").has_value());
    REQUIRE(store.find("bbbbbbbbbbbbbbbb").has_value());
    REQUIRE(store.all().size() == 1);
}

TEST_CASE("a supplied profile defaults to not displacing",
          "[core][ColorProfileOverride]")
{
    // The default matters: it is what the global default profile relies on to
    // keep leaving an embedded profile alone.
    SuppliedColorProfile supplied;
    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes.empty());
}
