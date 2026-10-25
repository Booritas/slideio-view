#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Types.h"

using namespace slideio::viewer::core;

TEST_CASE("channels are numbered from 1", "[core][ChannelLabel]")
{
    // The Metadata pane once read "Channel 0" where the Channels pane read
    // "Channel 1" for the same channel.
    CHECK(channelNumberLabel(0) == "Channel 1");
    CHECK(channelNumberLabel(1) == "Channel 2");
    CHECK(channelNumberLabel(9) == "Channel 10");
}
