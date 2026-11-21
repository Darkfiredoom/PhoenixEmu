// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/steady_pace.h"

using VideoCommon::SteadyPace;

namespace {
void Feed(SteadyPace& pace, double ms, int frames) {
    for (int i = 0; i < frames; ++i) {
        pace.AddFrame(ms);
    }
}
} // namespace

TEST_CASE("SteadyPace: no pace until enough frames", "[video_core]") {
    SteadyPace pace;
    Feed(pace, 16.7, 30);
    REQUIRE(pace.TargetFps() == 0.0);
}

TEST_CASE("SteadyPace: holds what 90% of frames make, in 5 fps steps", "[video_core]") {
    SteadyPace pace;
    // 60 fps with every eighth frame at 22 ms: 45.5 fps (46.8 with the leeway) -> 45.
    for (int i = 0; i < 240; ++i) {
        pace.AddFrame(i % 8 == 0 ? 22.0 : 16.7);
    }
    REQUIRE(pace.TargetFps() == 45.0);
}

TEST_CASE("SteadyPace: down at once, up only after three looks", "[video_core]") {
    SteadyPace pace;
    Feed(pace, 16.7, 120);
    REQUIRE(pace.TargetFps() == 60.0); // 59.9 fps is a steady 60
    Feed(pace, 30.0, 120);             // a heavy scene
    REQUIRE(pace.TargetFps() == 30.0);
    Feed(pace, 16.7, 90); // the window holds lighter frames now, but only two looks agree
    REQUIRE(pace.TargetFps() == 30.0);
    Feed(pace, 16.7, 120);
    REQUIRE(pace.TargetFps() == 60.0);
}

TEST_CASE("SteadyPace: loads and long hitches are left out", "[video_core]") {
    SteadyPace pace;
    Feed(pace, 10.0, 120);
    const double before = pace.TargetFps();
    Feed(pace, 2000.0, 20);
    REQUIRE(pace.TargetFps() == before);
}
