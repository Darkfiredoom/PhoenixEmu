// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QModelIndex>
#include <QPixmap>
#include <QRectF>

class QPainter;

/// The angle a case rests at: its front and its spine both show.
inline constexpr double CaseRestAngle = 39.6;

/// Draws a game's case as a box in 3D in `local` (the case's size, not turned), turned
/// `angle_deg` about its upright middle: 0 faces the eye, 39.6 shows the spine too, 90 only the
/// spine (a case standing on a shelf), 180 the back. Uses the game's case art (the CaseArt roles)
/// with the plastic rim, else the generated case with `picture`.
void DrawCaseBox(QPainter& p, const QModelIndex& index, const QRectF& local, qreal dpr,
                 qreal angle_deg, const QPixmap& picture);

/// The angle a cartridge rests at: its front and an edge both show.
inline constexpr double CartridgeRestAngle = 22.0;

/// Draws a game card (cartridge) as a solid in 3D in `local` (the cartridge frame's rectangle,
/// margin included, not turned), turned `angle_deg` about its upright middle: 0 faces the eye,
/// 180 shows its back (the player's photos: red on Switch 2, dark grey on Switch 1). Its rounded
/// edge is built from stacked outlines so the corners stay round as it turns.
void DrawCartridgeBox(QPainter& p, bool is_switch2, const QRectF& local, qreal dpr,
                      qreal angle_deg, const QPixmap& picture);
