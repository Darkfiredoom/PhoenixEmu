// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

/// The player's own choice of whether a game shows as a Switch 1 or a Switch 2 game (its badge,
/// case, cartridge, pictures and the library's Switch 1 / Switch 2 tabs), for when PhoenixEmu
/// can't tell a Switch 2 Edition upgrade apart. Kept in <config dir>/platforms.ini.
namespace PlatformOverride {

enum class Choice { Automatic, Switch1, Switch2 };

[[nodiscard]] Choice Get(u64 program_id);
void Set(u64 program_id, Choice choice);

} // namespace PlatformOverride
