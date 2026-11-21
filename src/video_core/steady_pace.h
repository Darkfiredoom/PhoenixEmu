// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace VideoCommon {

/// Frame pacing's Steady mode (after Triforce's Frame pacing > Steady): the frame rate this PC
/// holds where the game is now, from how long frames take on their own (without the pacing's
/// sleep). Frames are then shown at an even interval at that rate, never faster than the game
/// runs by itself: a little frame rate given up for frames that come evenly.
///
/// The rate is what 90% of the last 120 frames made, rounded down to 5 fps. It goes down at the
/// next look (every 30 frames, half a second at 60 fps) when a scene gets heavier, and up only
/// after three looks in a row say so, so one light moment does not bring the stutter back.
/// Frames over 250 ms (a load, a shader compile) are left out.
class SteadyPace {
public:
    static constexpr std::size_t Window = 120;
    static constexpr int LookEvery = 30;
    static constexpr int RaiseAfter = 3;
    static constexpr double StepFps = 5.0;
    static constexpr double MinFps = 20.0;
    static constexpr double MaxFps = 240.0;
    static constexpr double LongestFrameMs = 250.0;

    /// Takes one frame's own time; returns true when the target changed.
    bool AddFrame(double frame_ms) {
        if (!(frame_ms > 0.0) || frame_ms > LongestFrameMs) {
            return false;
        }
        times[next] = frame_ms;
        next = (next + 1) % Window;
        count = std::min(count + 1, Window);
        if (++since_look < LookEvery || count < Window / 2) {
            return false;
        }
        since_look = 0;
        return Look();
    }

    /// The pace in frames per second; 0 until enough frames were seen (no pacing yet).
    double TargetFps() const {
        return target;
    }

private:
    bool Look() {
        std::array<double, Window> sorted{};
        std::copy_n(times.begin(), count, sorted.begin());
        const std::size_t at = (count * 9) / 10; // the 90th percentile frame time
        std::nth_element(sorted.begin(), sorted.begin() + at, sorted.begin() + count);
        // 3% leeway: a game at a steady 60 (16.7 ms frames, 59.9 fps) is paced at 60, not 55.
        const double held = 1000.0 / sorted[at] * 1.03;
        const double candidate =
            std::clamp(std::floor(held / StepFps) * StepFps, MinFps, MaxFps);
        if (target == 0.0 || candidate < target) {
            const bool changed = candidate != target;
            target = candidate;
            raise_votes = 0;
            return changed;
        }
        if (candidate > target) {
            if (++raise_votes >= RaiseAfter) {
                target = candidate;
                raise_votes = 0;
                return true;
            }
            return false;
        }
        raise_votes = 0;
        return false;
    }

    std::array<double, Window> times{};
    std::size_t next = 0;
    std::size_t count = 0;
    int since_look = 0;
    int raise_votes = 0;
    double target = 0.0;
};

} // namespace VideoCommon
