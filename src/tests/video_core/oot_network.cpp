// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/settings.h"
#include "video_core/oot_network.h"

using namespace VideoCommon::OotNetwork;

TEST_CASE("The tonemap candidate is a draw of the output pass's input size, not its output",
          "[video_core][oot_network]") {
    // 1.1.1 at 1080p: the HDR scene (the input), the tonemapped image and the output.
    constexpr u64 scene = 0x8126a71000ULL;
    constexpr u64 tonemapped = 0x8128876000ULL;
    constexpr u64 output = 0x814b4ff000ULL;
    STATIC_REQUIRE(IsTonemapCandidate(tonemapped, 1920, 1080, 1920, 1080, output));
    STATIC_REQUIRE(IsTonemapCandidate(scene, 1920, 1080, 1920, 1080, output));
    STATIC_REQUIRE_FALSE(IsTonemapCandidate(output, 1920, 1080, 1920, 1080, output));
    // The game also draws into the tonemapped image's memory at half size earlier in the frame.
    STATIC_REQUIRE_FALSE(IsTonemapCandidate(tonemapped, 960, 540, 1920, 1080, output));
    // Before the first output pass, the input's size is not known.
    STATIC_REQUIRE_FALSE(IsTonemapCandidate(tonemapped, 1920, 1080, 0, 0, 0));
    STATIC_REQUIRE_FALSE(IsTonemapCandidate(0, 1920, 1080, 1920, 1080, output));
}

TEST_CASE("Ocarina of Time network bypass follows the setting and the game",
          "[video_core][oot_network]") {
    auto& setting = Settings::values.oot_anti_aliasing;
    const auto previous = setting.GetValue();

    setting.SetValue(Settings::OotAntiAliasing::Game);
    CHECK_FALSE(BypassEnabled(ProgramId));

    setting.SetValue(Settings::OotAntiAliasing::NoSharpening);
    CHECK(BypassEnabled(ProgramId));
    CHECK_FALSE(BypassEnabled(0x0400C3F00006E000ULL));

    setting.SetValue(previous);
}
