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

// AIRoute.h
// Path planning around areas to stay out of (the reach of static defences, anti-aircraft coverage) for the
// Expert computer player.  Plain geometry on synchronised numbers.

#pragma once

#include "Common/GameCommon.h"
#include "Common/GameType.h"

enum { AIROUTE_MAX_POINTS = 4, AIROUTE_MAX_CIRCLES = 24 };

/// An area to keep out of.
struct AIRouteCircle
{
	Coord3D	m_center;
	Real		m_radius;
};

/**
 * Waypoints from 'from' to 'to' whose legs stay out of the circles (a circle that contains 'from' or 'to' is not avoided).
 * Returns the number of waypoints written (0: the straight way is clear) or -1 when there is no way with at most
 * maxWaypoints bends that is not longer than maxDetourFactor times the straight way.  'extent' (may be null) keeps the
 * waypoints on the map.
 */
Int AIPlanDetour( const Coord3D &from, const Coord3D &to, const AIRouteCircle *circles, Int numCircles, Coord3D *waypoints,
	Int maxWaypoints, Real maxDetourFactor, const Region3D *extent );

/// Length of the path from 'from' through the waypoints to 'to'.
Real AIPathLength( const Coord3D &from, const Coord3D *via, Int numVia, const Coord3D &to );
