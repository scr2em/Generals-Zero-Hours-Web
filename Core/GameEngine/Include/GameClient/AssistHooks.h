/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// AssistHooks.h
// Hooks that let the shared message translators ask the player assists (Zero Hour only) a question without
// depending on them.  The assists set the hooks; with no assists the hooks stay empty and nothing changes.

#pragma once

#include "Lib/BaseType.h"

/// Called when the right mouse button goes down: true if the assists take the drag (a formation aim) and the camera must not scroll with it.
typedef Bool (*AssistRightDragHook)();
extern AssistRightDragHook TheAssistRightDragHook;

/// Called when the radar raises the "under attack" alert for the local player, with the place of the attack.
typedef void (*AssistAlertHook)( const struct Coord3D *pos );
extern AssistAlertHook TheAssistAlertHook;

/// How far out the player may zoom the camera, as a share of the game's own limit (1: as the game has it).  A setting of
/// the player (Options.ini), only for the view: the tactical view multiplies its highest camera height by it.
extern Real TheAssistCameraHeightScale;

/// Called for every command of a player that reaches the game (GameLogic::logicMessageDispatcher), for the info strip's
/// actions per minute.  It only counts: it must not change anything.
typedef void (*AssistCommandHook)( const class GameMessage *msg );
extern AssistCommandHook TheAssistCommandHook;
