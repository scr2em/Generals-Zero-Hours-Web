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

// AssistUICoverage.cpp
// The defence coverage view (display only): range rings of the player's own defensive structures, ground and air
// separately, and the edge of each base with the stretches that none of the defences reach.  What a structure shoots at
// comes from its weapons (the combat figures of the thing template: ground / air anti-masks and range), not from its name.
// Nothing in the simulation depends on this view.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>
#include <math.h>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/View.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	Bool s_on = FALSE;

	struct Defence
	{
		Coord3D	m_pos;
		Real		m_range;
		Bool		m_ground;
		Bool		m_air;
	};
	struct Building
	{
		Coord3D	m_pos;
		Real		m_radius;
	};
	struct Collect
	{
		std::vector<Defence>	m_defences;
		std::vector<Building>	m_buildings;
	};

	void collectObject( Object *obj, void *userData )
	{
		Collect *c = (Collect *)userData;
		if (obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->isOffMap())
			return;
		if (obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) || obj->isKindOf( KINDOF_DEFENSIVE_WALL ))
			return;	// wall pieces are no base
		Building b;
		b.m_pos = *obj->getPosition();
		b.m_radius = obj->getGeometryInfo().getBoundingCircleRadius();
		c->m_buildings.push_back( b );

		const AICombatFigures *f = AICombatModel::figures( obj->getTemplate() );
		if (f && f->m_armed && f->m_range > 0.0f && (f->m_canHitGround || f->m_canHitAir) && !obj->isDisabled())
		{
			Defence d;
			d.m_pos = b.m_pos;
			d.m_range = f->m_range;
			d.m_ground = f->m_canHitGround;
			d.m_air = f->m_canHitAir;
			c->m_defences.push_back( d );
		}
	}

	Real dist2D( const Coord3D &a, const Coord3D &b )
	{
		return sqrtf( (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) );
	}

	Bool project( Real x, Real y, ICoord2D *s )
	{
		Coord3D p;
		p.x = x;
		p.y = y;
		p.z = TheTerrainLogic->getGroundHeight( x, y ) + 2.0f;
		return TheTacticalView->worldToScreen( &p, s );
	}

	/// A circle on the ground as short lines; segments that are off screen are left out.
	void ring( const Coord3D &c, Real radius, Color color, Int width, Int segments )
	{
		ICoord2D prev;
		Bool havePrev = FALSE;
		for (Int i = 0; i <= segments; ++i)
		{
			const Real a = 6.2831853f * (Real)i / (Real)segments;
			ICoord2D s;
			const Bool ok = project( c.x + radius * cosf( a ), c.y + radius * sinf( a ), &s );
			if (ok && havePrev)
				AssistUI::line( prev.x, prev.y, s.x, s.y, width, color );
			prev = s;
			havePrev = ok;
		}
	}
}

//-------------------------------------------------------------------------------------------------
static Bool coverageIsOn()
{
	return s_on;
}

//-------------------------------------------------------------------------------------------------
static void coverageToggle()
{
	AssistCoverageUI::toggle();
}

//-------------------------------------------------------------------------------------------------
void AssistCoverageUI::init()
{
	AssistUI::ToolbarEntry e;
	e.m_id = 1;
	e.m_label = L"Coverage";
	e.m_tip = L"Defence coverage view: range rings and gaps in the base edge";
	e.m_option = &TheAssistOptions.m_coverage;
	e.m_isOn = &coverageIsOn;
	e.m_toggle = &coverageToggle;
	AssistUI::addToolbarEntry( e );
}

//-------------------------------------------------------------------------------------------------
Bool AssistCoverageUI::on()
{
	return s_on && TheAssistOptions.m_coverage;
}

//-------------------------------------------------------------------------------------------------
void AssistCoverageUI::toggle()
{
	s_on = !s_on;
}

//-------------------------------------------------------------------------------------------------
void AssistCoverageUI::reset()
{
	s_on = FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AssistCoverageUI::translate( const GameMessage *msg )
{
	if (msg->getType() != GameMessage::MSG_META_ASSIST_COVERAGE)
		return FALSE;
	if (!TheAssistOptions.m_coverage || !AssistUI::playing())
		return FALSE;
	toggle();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssistCoverageUI::drawOverlays( View *view )
{
	if (!on() || TheTacticalView == nullptr)
		return;
	Player *local = ThePlayerList->getLocalPlayer();
	if (local == nullptr)
		return;

	Collect all;
	local->iterateObjects( collectObject, &all );

	const Color groundRing = GameMakeColor( 255, 150, 40, 170 );
	const Color airRing = GameMakeColor( 70, 190, 255, 170 );

	// the range rings
	for (size_t i = 0; i < all.m_defences.size(); ++i)
	{
		const Defence &d = all.m_defences[i];
		if (d.m_ground)
			ring( d.m_pos, d.m_range, groundRing, 1, 48 );
		if (d.m_air)
			ring( d.m_pos, d.m_air && d.m_ground ? d.m_range - 5.0f : d.m_range, airRing, 1, 48 );
	}

	// the bases: buildings that are close together (single link, 450 apart)
	const size_t n = all.m_buildings.size();
	std::vector<Int> cluster( n, -1 );
	Int clusters = 0;
	for (size_t i = 0; i < n; ++i)
	{
		if (cluster[i] >= 0)
			continue;
		cluster[i] = clusters;
		std::vector<size_t> stack;
		stack.push_back( i );
		while (!stack.empty())
		{
			const size_t a = stack.back();
			stack.pop_back();
			for (size_t b = 0; b < n; ++b)
			{
				if (cluster[b] < 0 && dist2D( all.m_buildings[a].m_pos, all.m_buildings[b].m_pos ) < 450.0f)
				{
					cluster[b] = clusters;
					stack.push_back( b );
				}
			}
		}
		++clusters;
	}

	Int uncoveredGround = 0, uncoveredAir = 0;
	Int groundDefences = 0, airDefences = 0;
	for (size_t i = 0; i < all.m_defences.size(); ++i)
	{
		groundDefences += all.m_defences[i].m_ground ? 1 : 0;
		airDefences += all.m_defences[i].m_air ? 1 : 0;
	}
	for (Int c = 0; c < clusters; ++c)
	{
		Coord3D centre;
		centre.x = centre.y = centre.z = 0.0f;
		Int count = 0;
		for (size_t i = 0; i < n; ++i)
		{
			if (cluster[i] == c)
			{
				centre.x += all.m_buildings[i].m_pos.x;
				centre.y += all.m_buildings[i].m_pos.y;
				++count;
			}
		}
		if (count == 0)
			continue;
		centre.x /= (Real)count;
		centre.y /= (Real)count;
		Real radius = 0.0f;
		for (size_t i = 0; i < n; ++i)
		{
			if (cluster[i] == c)
			{
				const Real r = dist2D( centre, all.m_buildings[i].m_pos ) + all.m_buildings[i].m_radius;
				if (r > radius)
					radius = r;
			}
		}
		radius += 40.0f;

		// the edge, in steps of five degrees: is every step within reach of a defence?
		const Int steps = 72;
		for (Int layer = 0; layer < 2; ++layer)
		{
			const Bool air = layer == 1;
			const Real edge = radius + (air ? 28.0f : 0.0f);
			ICoord2D prev;
			Bool havePrev = FALSE;
			for (Int i = 0; i <= steps; ++i)
			{
				const Real a = 6.2831853f * (Real)i / (Real)steps;
				const Real x = centre.x + edge * cosf( a );
				const Real y = centre.y + edge * sinf( a );
				// covered when some defence of the right kind reaches the middle of this step
				const Real am = 6.2831853f * ((Real)i - 0.5f) / (Real)steps;
				const Real mx = centre.x + edge * cosf( am );
				const Real my = centre.y + edge * sinf( am );
				Bool covered = FALSE;
				for (size_t d = 0; d < all.m_defences.size() && !covered; ++d)
				{
					const Defence &df = all.m_defences[d];
					if ((air ? df.m_air : df.m_ground) && sqrtf( (df.m_pos.x - mx) * (df.m_pos.x - mx) + (df.m_pos.y - my) * (df.m_pos.y - my) ) <= df.m_range)
						covered = TRUE;
				}
				if (i > 0 && !covered)
				{
					if (air)
						++uncoveredAir;
					else
						++uncoveredGround;
				}
				ICoord2D s;
				const Bool ok = project( x, y, &s );
				if (ok && havePrev && i > 0)
				{
					Color col;
					Int width;
					if (covered)
					{
						col = air ? GameMakeColor( 60, 150, 220, 90 ) : GameMakeColor( 70, 200, 90, 110 );
						width = 1;
					}
					else
					{
						col = air ? GameMakeColor( 200, 90, 255, 255 ) : GameMakeColor( 255, 50, 40, 255 );
						width = 3;
					}
					AssistUI::line( prev.x, prev.y, s.x, s.y, width, col );
				}
				prev = s;
				havePrev = ok;
			}
		}
	}

	// for the tests: what the view shows, printed when it changes
	{
		static Int last[5] = { -1, -1, -1, -1, -1 };
		const Int now[5] = { groundDefences, airDefences, clusters, uncoveredGround, uncoveredAir };
		if (TheAssistOptions.m_debug && memcmp( last, now, sizeof( last ) ) != 0)
		{
			memcpy( last, now, sizeof( last ) );
			ASSIST_DEBUG(( "ASSIST coverage ground defences=%d air defences=%d bases=%d uncovered ground steps=%d air steps=%d", groundDefences, airDefences, clusters, uncoveredGround, uncoveredAir ));
		}
	}

	// the key
	const UnicodeString key = L"Defence coverage: orange = ground range, blue = air range; red / purple = base edge no defence reaches (ground / air)";
	AssistUI::text( key, AssistUI::px( 8 ), AssistUI::px( 22 ), GameMakeColor( 255, 255, 255, 255 ), 9 );
}
