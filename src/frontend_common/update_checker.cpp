// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cctype>
#include <charconv>
#include <utility>
#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_checker.h"

namespace UpdateChecker {

namespace {

/// build-<n>, or build-<n>.<point> for a fix on top of build n (32.1 comes after 32, before 33).
std::optional<std::pair<u64, u64>> ParseBuildNumber(std::string_view tag) {
    constexpr std::string_view prefix = "build-";
    if (!tag.starts_with(prefix)) {
        return std::nullopt;
    }
    tag.remove_prefix(prefix.size());
    const char* const last = tag.data() + tag.size();
    u64 number{};
    auto [end, error] = std::from_chars(tag.data(), last, number);
    if (error != std::errc{} || end == tag.data() || number == 0) {
        return std::nullopt;
    }
    u64 point{};
    if (end != last) {
        if (*end != '.' || end + 1 == last || !std::isdigit(static_cast<unsigned char>(end[1]))) {
            return std::nullopt;
        }
        const auto [point_end, point_error] = std::from_chars(end + 1, last, point);
        if (point_error != std::errc{} || point_end != last || point == 0) {
            return std::nullopt;
        }
    }
    return std::make_pair(number, point);
}

} // namespace

Failure ClassifyFailure(int status) {
    if (status == 403 || status == 429) {
        return Failure::RateLimited;
    }
    return status >= 400 ? Failure::Http : Failure::Network;
}

std::optional<Common::Net::Release> SelectUpdate(std::span<const Common::Net::Release> releases,
                                                 std::string_view build_tag,
                                                 std::string_view platform,
                                                 Common::Net::LinuxPackage package) {
    auto newest = ParseBuildNumber(build_tag);
    if (!newest) {
        return std::nullopt;
    }

    const Common::Net::Release* selected = nullptr;
    for (const auto& release : releases) {
        const auto number = ParseBuildNumber(release.tag);
        if (number && *number > *newest && !release.GetPlatformAssets(platform, package).empty()) {
            newest = number;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

std::string_view Platform() {
    if (!std::string_view{Common::g_build_release_platform}.empty()) {
        return Common::g_build_release_platform;
    }
#ifdef _WIN32
    return "windows-x86_64";
#elif defined(__linux__)
    return "linux-x86_64";
#else
    return "";
#endif
}

std::string_view BuildTag() {
    if (!std::string_view{Common::g_build_release_tag}.empty()) {
        return Common::g_build_release_tag;
    }
    return "build-0";
}

CheckResult Check() {
    const auto platform = Platform();
    const auto build_tag = BuildTag();
    if (platform.empty()) {
        return {};
    }

    const auto list = Common::Net::GetReleases();
    if (!list.ok) {
        const auto failure = ClassifyFailure(list.status);
        LOG_WARNING(Frontend, "Update check failed with status {}", list.status);
        return {.failure = failure, .status = list.status};
    }

    auto update = SelectUpdate(list.releases, build_tag, platform,
                               Common::Net::CurrentLinuxPackage());
    if (update) {
        LOG_INFO(Frontend, "Received update {}", update->title);
    }
    return {.update = std::move(update)};
}

std::optional<Common::Net::Release> GetUpdate() {
    return Check().update;
}

} // namespace UpdateChecker
