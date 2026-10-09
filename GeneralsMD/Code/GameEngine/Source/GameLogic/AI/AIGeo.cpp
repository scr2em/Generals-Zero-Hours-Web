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

// AIGeo.cpp
// Terrain-aware base defence of the Expert computer player.
//
// From the pathfinder and the terrain the AI works out where an attack on its base comes in:
//   * The ways in: the ground paths from the enemy start positions to the base are walked to the place where they cross the
//     perimeter.  When terrain closes the perimeter (cliffs, water, buildings) into several gaps, every gap that the enemy can
//     reach is a way in; its share of the expected attacks falls with the length of the way from the enemy.
//   * The chokepoints: on the way in, going outwards, the width of the passable ground is measured across the path; the
//     narrowest place (or a bridge) is the chokepoint of that way in.
//   * The expected threat of a way in is the share from the paths, mixed with what the enemy model has seen of enemy ground
//     units that came near it.
// The ways in decide where defence structures of the build list (or of the "build base defence" script action) are placed
// (covering the chokepoint, on high ground, behind it, spread over the ways in by threat), and where the army waits (the
// rally point is on the main way in, between the threat and the economy).
//
// The orientation of the base layout (the build list is a fixed shape that the original code turns by one angle for every
// start position) can also be turned to face the enemy start (variant word "layout").
//
// Only the terrain, the map's start positions (public in a skirmish) and what the player has seen are used.  Everything is
// computed with game logic functions in a fixed order, so it is deterministic.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// ---- the ground -------------------------------------------------------------------------------------------------------
static const Int PATH_CELLS = 4;			// width of the paths asked from the pathfinder (cells of 10 units)
static const Real STEP = 10.0f;				// one pathfinder cell
enum { RING_SAMPLES = 72, POLY_MAX = 512 };

static Bool groundOpen(const Coord3D &p)
{
	Pathfinder *finder = TheAI->pathfinder();
	PathfindCell *cell = finder->getCell(LAYER_GROUND, &p);
	return cell != nullptr && finder->validMovementPosition(FALSE, LOCOMOTORSURFACE_GROUND, cell);
}

/// The nodes of a ground path (start to end).
struct GeoPoly
{
	Coord3D	p[POLY_MAX];
	Bool		bridge[POLY_MAX];
	Int			n;
	GeoPoly() : n(0) {}
};

static void polyFromPath(Path *path, GeoPoly &poly)
{
	poly.n = 0;
	if (path == nullptr)
		return;
	Int count = 0;
	for (PathNode *node = path->getFirstNode(); node; node = node->getNext())
		++count;
	const Int skip = count > POLY_MAX ? (count + POLY_MAX - 1) / POLY_MAX : 1;
	Int i = 0;
	for (PathNode *node = path->getFirstNode(); node && poly.n < POLY_MAX; node = node->getNext(), ++i)
	{
		if (i % skip != 0 && node->getNext() != nullptr)
			continue;
		poly.p[poly.n] = *node->getPosition();
		poly.bridge[poly.n] = node->getLayer() != LAYER_GROUND;
		++poly.n;
	}
}

static Real polyLength(const GeoPoly &poly)
{
	Real len = 0.0f;
	for (Int i = 1; i < poly.n; ++i)
		len += dist2D(poly.p[i - 1], poly.p[i]);
	return len;
}

/// Index of the first node (from the start) that is within 'radius' of 'center'; -1 if none.
static Int polyEnter(const GeoPoly &poly, const Coord3D &center, Real radius)
{
	for (Int i = 0; i < poly.n; ++i)
		if (dist2D(poly.p[i], center) <= radius)
			return i;
	return -1;
}

/// Passable span across the direction of travel at c.
static Real spanAt(const Coord3D &c, Real dx, Real dy, Real maxHalf)
{
	if (!groundOpen(c))
		return 0.0f;
	const Real len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f)
		return STEP;
	const Real nx = -dy / len, ny = dx / len;
	Real half[2] = { 0.0f, 0.0f };
	for (Int side = 0; side < 2; ++side)
	{
		const Real sign = side ? 1.0f : -1.0f;
		for (Real d = STEP; d <= maxHalf; d += STEP)
		{
			Coord3D q = c;
			q.x += sign * nx * d;
			q.y += sign * ny * d;
			if (!groundOpen(q))
				break;
			half[side] = d;
		}
	}
	return half[0] + half[1] + STEP;
}

/// The nearest open pathfinder cell to a place (a structure may stand on it): rings of cells around it.
static Bool openNear(const Coord3D &center, Int maxCells, Coord3D *out)
{
	for (Int d = 0; d <= maxCells; ++d)
	{
		Real bestDist = 1.0e9f;
		Bool found = FALSE;
		for (Int dy = -d; dy <= d; ++dy)
		{
			for (Int dx = -d; dx <= d; ++dx)
			{
				if (d > 0 && abs(dx) != d && abs(dy) != d)
					continue;
				Coord3D q = center;
				q.x += dx * STEP;
				q.y += dy * STEP;
				if (!groundOpen(q))
					continue;
				const Real dd = (Real)(dx * dx + dy * dy);
				if (dd < bestDist)
				{
					bestDist = dd;
					*out = q;
					found = TRUE;
				}
			}
		}
		if (found)
			return TRUE;
	}
	return FALSE;
}

// ---- the ground that can be reached from the base ----------------------------------------------------------------------------
// Terrain closes the perimeter with cliffs: the top of a cliff ring is flat ground that nobody can walk on from the base side.
// So the rings are scanned for ground that is connected to the base, which a flood fill over the pathfinder cells tells.
enum { FLOOD_HALF = 125, FLOOD_SIZE = 2 * FLOOD_HALF + 1 };			// +-1250 units: base radius + perimeter margin + the rings looked at
static UnsignedByte s_reach[FLOOD_SIZE * FLOOD_SIZE];
static Int s_queue[FLOOD_SIZE * FLOOD_SIZE];
static Coord3D s_floodCenter;

static void floodFrom(const Coord3D &center)
{
	memset(s_reach, 0, sizeof(s_reach));
	s_floodCenter = center;
	// the nearest open cell to the center
	Coord3D startPos;
	if (!openNear(center, 8, &startPos))
		return;
	const Int sx = (Int)floorf((startPos.x - center.x) / STEP + 0.5f);
	const Int sy = (Int)floorf((startPos.y - center.y) / STEP + 0.5f);
	Int head = 0, tail = 0;
	s_reach[(sy + FLOOD_HALF) * FLOOD_SIZE + sx + FLOOD_HALF] = 1;
	s_queue[tail++] = (sy + FLOOD_HALF) * FLOOD_SIZE + sx + FLOOD_HALF;
	static const Int ox[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const Int oy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	while (head < tail)
	{
		const Int cur = s_queue[head++];
		const Int cx = cur % FLOOD_SIZE, cy = cur / FLOOD_SIZE;
		for (Int k = 0; k < 8; ++k)
		{
			const Int nx = cx + ox[k], ny = cy + oy[k];
			if (nx < 0 || ny < 0 || nx >= FLOOD_SIZE || ny >= FLOOD_SIZE || s_reach[ny * FLOOD_SIZE + nx])
				continue;
			Coord3D q = center;
			q.x += (nx - FLOOD_HALF) * STEP;
			q.y += (ny - FLOOD_HALF) * STEP;
			if (!groundOpen(q))
				continue;
			if (k >= 4)
			{
				// no squeezing through the corner between two blocked cells
				Coord3D a = q, b = q;
				a.x -= ox[k] * STEP;
				b.y -= oy[k] * STEP;
				if (!groundOpen(a) || !groundOpen(b))
					continue;
			}
			s_reach[ny * FLOOD_SIZE + nx] = 1;
			s_queue[tail++] = ny * FLOOD_SIZE + nx;
		}
	}
}

static Bool reachedByFlood(const Coord3D &p)
{
	const Int gx = (Int)floorf((p.x - s_floodCenter.x) / STEP + 0.5f) + FLOOD_HALF;
	const Int gy = (Int)floorf((p.y - s_floodCenter.y) / STEP + 0.5f) + FLOOD_HALF;
	if (gx < 0 || gy < 0 || gx >= FLOOD_SIZE || gy >= FLOOD_SIZE)
		return TRUE;		// beyond the window nothing is known: not a wall
	return s_reach[gy * FLOOD_SIZE + gx] != 0;
}

/// Can a ground unit walk from one place to the other (a site on top of a cliff rim cannot be built on)?
static Bool reachableFrom(const Coord3D &from, const Coord3D &to)
{
	Path *path = TheAI->pathfinder()->findGroundPath(&from, &to, PATH_CELLS, FALSE);
	if (path == nullptr)
		return FALSE;
	const PathNode *last = path->getLastNode();
	const Bool ok = last != nullptr && dist2D(*last->getPosition(), to) < 40.0f;
	deleteInstance(path);
	return ok;
}

static Bool isDefenceTemplate(const ThingTemplate *tmpl)
{
	if (tmpl == nullptr || !tmpl->isKindOf(KINDOF_STRUCTURE) || !tmpl->isKindOf(KINDOF_FS_BASE_DEFENSE))
		return FALSE;
	const AICombatFigures *f = AICombatModel::figures(tmpl);
	return f != nullptr && f->m_armed && f->m_canHitGround;
}

// ---- switches -----------------------------------------------------------------------------------------------------------
Bool AIStrategy::geoOn() const
{
	return (skill().m_useGeo || m_ai->isFeatureForced(AIPlayer::AIF_GEO)) && !m_ai->isFeatureOff(AIPlayer::AIF_GEO);
}

Bool AIStrategy::layoutOn() const
{
	return (skill().m_useLayout || m_ai->isFeatureForced(AIPlayer::AIF_LAYOUT)) && !m_ai->isFeatureOff(AIPlayer::AIF_LAYOUT);
}

void AIStrategy::geoReset()
{
	m_numEntrances = 0;
	memset(m_entrances, 0, sizeof(m_entrances));
	m_geoReady = FALSE;
	m_geoTries = 0;
	m_nextGeo = 0;
	m_nextGeoWeights = 0;
	m_geoMain = -1;
	m_numGeoSites = 0;
	memset(m_geoSites, 0, sizeof(m_geoSites));
	m_geoMoved = 0;
	m_geoWall = FALSE;
	m_geoRing = 0.0f;
	m_geoPlaced = 0;
	m_geoPlacedFixed = 0;
	m_geoRallyMoves = 0;
}

// ---- the start positions of the enemies -----------------------------------------------------------------------------------
/// The start positions of the enemies (public knowledge in a skirmish); without enemies in the player list, all other starts.
Int AIStrategy::geoEnemyStarts( Coord3D *out, Int maxStarts, const Coord3D *notNear ) const
{
	Int n = 0;
	for (Int i = 0; i < ThePlayerList->getPlayerCount() && n < maxStarts; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (p == nullptr || p == m_player || p->getMpStartIndex() < 0 || !p->isPlayerActive())
			continue;
		if (m_player->getRelationship(p->getDefaultTeam()) != ENEMIES)
			continue;
		AsciiString name;
		name.format("Player_%d_Start", p->getMpStartIndex() + 1);
		Waypoint *way = TheTerrainLogic->getWaypointByName(name);
		if (way == nullptr)
			continue;
		if (notNear && dist2D(*way->getLocation(), *notNear) < 60.0f)
			continue;
		out[n++] = *way->getLocation();
	}
	if (n == 0)
	{
		for (Int k = 1; k <= 8 && n < maxStarts; ++k)
		{
			if (k == m_player->getMpStartIndex() + 1)
				continue;
			AsciiString name;
			name.format("Player_%d_Start", k);
			Waypoint *way = TheTerrainLogic->getWaypointByName(name);
			if (way == nullptr)
				continue;
			if (notNear && dist2D(*way->getLocation(), *notNear) < 60.0f)
				continue;
			out[n++] = *way->getLocation();
		}
	}
	return n;
}

// ---- the layout ------------------------------------------------------------------------------------------------------------
/// The turn of the build list for this start position: the original code turns every layout by 135 degrees (or by a step of the
/// start's place in a 3 x 3 grid when the map option is on).  With the word "layout" the layout turns to face the enemy:
/// the direction to the enemy starts plus 90 degrees, which is the original turn for the start in the corner that faces the
/// other corner and the mirror image for the opposite start.
Bool AIStrategy::layoutAngle( const Coord3D &start, Real *angle ) const
{
	if (!layoutOn())
		return FALSE;
	Coord3D starts[4];
	const Int n = geoEnemyStarts(starts, 4, &start);
	if (n == 0)
		return FALSE;
	Real sx = 0.0f, sy = 0.0f;
	for (Int i = 0; i < n; ++i)
	{
		sx += starts[i].x - start.x;
		sy += starts[i].y - start.y;
	}
	if (fabsf(sx) + fabsf(sy) < 1.0f)
		return FALSE;
	*angle = atan2f(sy, sx) + PI / 2.0f;
	return TRUE;
}

// ---- the ways in ----------------------------------------------------------------------------------------------------------------
/// Narrowest place on the way in: walks the path outwards from the crossing and measures the passable span across it.
void AIStrategy::geoFindChoke( const GeoPoly &poly, Int crossIdx, AIEntrance &e, Real maxFromCross ) const
{
	const AISkillSettings &sk = skill();
	e.m_choke = e.m_cross;
	e.m_choked = FALSE;
	e.m_bridge = FALSE;
	e.m_width = 0.0f;
	if (crossIdx < 0 || crossIdx >= poly.n)
		return;
	Real best = 1.0e9f, widest = 0.0f;
	Coord3D bestPos = e.m_cross;
	Real travelled = 0.0f;
	Coord3D last = poly.p[crossIdx];
	Real crossSpan = 0.0f;
	for (Int i = crossIdx; i >= 0 && travelled <= sk.m_geoLookOut; --i)
	{
		travelled += dist2D(last, poly.p[i]);
		last = poly.p[i];
		if (dist2D(poly.p[i], e.m_cross) > maxFromCross)
			break;			// the way in is the stretch near the crossing; the path may wind on to somewhere else
		if (poly.bridge[i])
		{
			// a bridge is as wide as a road: the way in is forced over it
			if (!e.m_bridge)
			{
				e.m_bridge = TRUE;
				e.m_choke = poly.p[i];
				e.m_choked = TRUE;
				e.m_width = 40.0f;
			}
			continue;
		}
		const Int a = i > 1 ? i - 2 : 0, b = i + 2 < poly.n ? i + 2 : poly.n - 1;
		const Real span = spanAt(poly.p[i], poly.p[b].x - poly.p[a].x, poly.p[b].y - poly.p[a].y, sk.m_geoChokeWidth + 60.0f);
		if (i == crossIdx)
			crossSpan = span;
		if (span > widest)
			widest = span;
		if (span > 0.0f && span < best)
		{
			best = span;
			bestPos = poly.p[i];
		}
	}
	if (e.m_bridge)
		return;
	e.m_width = crossSpan;
	if (best <= sk.m_geoChokeWidth && best < 0.75f * widest)
	{
		e.m_choked = TRUE;
		e.m_choke = bestPos;
		e.m_width = best;
	}
}

Bool AIStrategy::geoCompute()
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base) || m_ai->m_baseRadius <= 0.0f)
		return FALSE;
	const AISkillSettings &sk = skill();
	const Real baseR = m_ai->m_baseRadius;
	const Real r0 = baseR + sk.m_geoRing;

	Coord3D starts[3];
	const Int numStarts = geoEnemyStarts(starts, 3, nullptr);
	if (numStarts == 0)
	{
		AI_TRACE("GEO: no enemy start position known");
		return TRUE;	// nothing to compute: finished, with no ways in
	}

	for (Int s = 0; s < numStarts; ++s)
		AI_TRACE("GEO: enemy start %d at (%.0f,%.0f), %.0f from the base", s + 1, starts[s].x, starts[s].y, dist2D(starts[s], base));
	Pathfinder *finder = TheAI->pathfinder();

	// A structure may stand on the start position or on the center of the base: paths begin and end on open ground.
	Coord3D from[3];
	for (Int s = 0; s < numStarts; ++s)
	{
		from[s] = starts[s];
		openNear(starts[s], 12, &from[s]);
	}
	Coord3D goal = base;
	openNear(base, 12, &goal);

	// The paths from the enemy starts to the base; they also tell whether the pathfinder is ready.
	static GeoPoly paths[3];
	Real lengths[3] = { 0.0f, 0.0f, 0.0f };
	Bool have[3] = { FALSE, FALSE, FALSE };
	Int found = 0;
	for (Int s = 0; s < numStarts; ++s)
	{
		Path *path = finder->findGroundPath(&from[s], &goal, PATH_CELLS, FALSE);
		if (path == nullptr)
		{
			Path *back = finder->findGroundPath(&goal, &from[s], PATH_CELLS, FALSE);
			Coord3D mid = goal;
			mid.x = (goal.x + from[s].x) * 0.5f;
			mid.y = (goal.y + from[s].y) * 0.5f;
			Path *half = finder->findGroundPath(&from[s], &mid, PATH_CELLS, FALSE);
			AI_TRACE("GEO: no path from enemy start %d to the base center (start open %d, base center open %d); reverse path %s; start to the middle %s",
				s + 1, groundOpen(from[s]) ? 1 : 0, groundOpen(goal) ? 1 : 0, back ? "yes" : "no", half ? "yes" : "no");
			deleteInstance(back);
			deleteInstance(half);
			continue;
		}
		polyFromPath(path, paths[s]);
		deleteInstance(path);
		if (paths[s].n < 2)
			continue;
		lengths[s] = polyLength(paths[s]);
		have[s] = TRUE;
		++found;
	}
	if (found == 0)
		return FALSE;

	// Does terrain close the perimeter?  Look at rings of growing radius for the first one that is cut into gaps.
	floodFrom(base);
	Int arcCount = 0;
	Real ringUsed = 0.0f;
	struct Arc { Real angle; Real width; };
	Arc arcs[8];
	Int bestBlocked = 0;
	for (Int k = 0; k < 10; ++k)
	{
		const Real radius = r0 + 30.0f * k;
		if (radius > (Real)(FLOOD_HALF - 5) * STEP)
			break;		// beyond the flooded window
		Bool open[RING_SAMPLES];
		Int blocked = 0, offMap = 0;
		for (Int i = 0; i < RING_SAMPLES; ++i)
		{
			Coord3D q = base;
			const Real a = 2.0f * PI * i / RING_SAMPLES;
			q.x += cosf(a) * radius;
			q.y += sinf(a) * radius;
			open[i] = reachedByFlood(q);
			if (!open[i])
			{
				++blocked;
				if (finder->getCell(LAYER_GROUND, &q) == nullptr)
					++offMap;			// beyond the edge of the map: not a wall
			}
		}
		if (m_trace)
		{
			char pattern[RING_SAMPLES + 1];
			for (Int i = 0; i < RING_SAMPLES; ++i)
				pattern[i] = open[i] ? '1' : '0';
			pattern[RING_SAMPLES] = 0;
			AI_TRACE("GEO: ring %.0f reachable from the base: %s", radius, pattern);
		}
		if (blocked == 0 || blocked == RING_SAMPLES)
			continue;
		// runs of open samples, starting after a blocked one
		Int start = 0;
		while (open[start])
			++start;		// a blocked sample exists (blocked > 0), so no run wraps over the start
		Int runs = 0;
		Arc runList[8];
		Int i = 0;
		while (i < RING_SAMPLES && runs < 8)
		{
			const Int idx = (start + i) % RING_SAMPLES;
			if (!open[idx])
			{
				++i;
				continue;
			}
			Int len = 0;
			while (i + len < RING_SAMPLES && open[(start + i + len) % RING_SAMPLES])
				++len;
			const Real mid = (Real)start + (Real)i + 0.5f * (Real)(len - 1);
			runList[runs].angle = 2.0f * PI * mid / RING_SAMPLES;
			runList[runs].width = (len + 1) * 2.0f * PI * radius / RING_SAMPLES;
			++runs;
			i += len;
		}
		// A wall: a quarter of the perimeter (where there is a map) is closed.  The ring with the most closed ground is the rim.
		const Int walled = blocked - offMap;
		if (runs < 1 || walled * 4 < RING_SAMPLES - offMap || walled <= bestBlocked)
			continue;
		bestBlocked = walled;
		for (Int r = 0; r < runs; ++r)
			arcs[r] = runList[r];
		arcCount = runs;
		ringUsed = radius;
	}

	Int count = 0;
	AIEntrance list[8];
	memset(list, 0, sizeof(list));
	Real priors[8];

	for (Int a = 0; a < arcCount; ++a)
		AI_TRACE("GEO: gap %d in the terrain ring of radius %.0f: bearing %.0f deg, %.0f wide", a + 1, ringUsed, arcs[a].angle * 180.0f / PI, arcs[a].width);
	if (arcCount > 0)
	{
		m_geoWall = TRUE;
		m_geoRing = ringUsed;
		// Every gap the enemy can reach is a way in.  Its share: from the length of the way from each enemy start.
		for (Int a = 0; a < arcCount; ++a)
		{
			AIEntrance &e = list[count];
			e.m_cross = base;
			e.m_cross.x += cosf(arcs[a].angle) * ringUsed;
			e.m_cross.y += sinf(arcs[a].angle) * ringUsed;
			e.m_cross.z = TheTerrainLogic->getGroundHeight(e.m_cross.x, e.m_cross.y);
			e.m_width = arcs[a].width;
			priors[count] = 0.0f;
			Bool reached = FALSE;
			Real bestLen = 1.0e9f;
			static GeoPoly poly;
			Int bestIdx = -1;
			for (Int s = 0; s < numStarts; ++s)
			{
				if (!have[s])
					continue;
				Path *path = finder->findGroundPath(&from[s], &e.m_cross, PATH_CELLS, FALSE);
				if (path == nullptr)
				{
					AI_TRACE("GEO: gap at (%.0f,%.0f), %.0f wide: no path from the enemy start %d", e.m_cross.x, e.m_cross.y, arcs[a].width, s + 1);
					continue;
				}
				GeoPoly tmp;
				polyFromPath(path, tmp);
				deleteInstance(path);
				if (tmp.n < 2 || dist2D(tmp.p[tmp.n - 1], e.m_cross) > 50.0f)
				{
					AI_TRACE("GEO: gap at (%.0f,%.0f), %.0f wide: the path from the enemy start %d ends %.0f away", e.m_cross.x, e.m_cross.y, arcs[a].width, s + 1,
						tmp.n > 0 ? dist2D(tmp.p[tmp.n - 1], e.m_cross) : -1.0f);
					continue;
				}
				const Real len = polyLength(tmp);
				if (len < bestLen)
				{
					bestLen = len;
					poly = tmp;
					bestIdx = tmp.n - 1;
				}
				// share of this start: shorter ways are more likely
				if (lengths[s] > 0.0f)
				{
					const Real ratio = lengths[s] / (len + 1.0f);
					priors[count] += (len <= 2.2f * lengths[s]) ? (ratio * ratio * ratio) / numStarts : 0.03f / numStarts;
				}
				reached = TRUE;
			}
			if (!reached)
				continue;
			geoFindChoke(poly, bestIdx, e, 100.0f);
			if (arcs[a].width < e.m_width || e.m_width <= 0.0f)
				e.m_width = arcs[a].width;			// the span is measured across the path, which may cross the gap slantwise
			if (!e.m_choked)
			{
				// a gap in a wall is a chokepoint by itself
				e.m_choke = e.m_cross;
				e.m_choked = TRUE;
				if (e.m_width <= 0.0f)
					e.m_width = arcs[a].width;
			}
			++count;
		}
	}
	else
	{
		m_geoWall = FALSE;
		m_geoRing = r0;
		// Open ground: the crossing of every path is a way in; paths that cross near each other are one.
		for (Int s = 0; s < numStarts && count < 8; ++s)
		{
			if (!have[s])
				continue;
			const Int idx = polyEnter(paths[s], base, r0);
			if (idx < 0)
				continue;
			Coord3D cross = paths[s].p[idx];
			if (idx > 0)
			{
				// the point of the segment on the circle
				const Coord3D &a = paths[s].p[idx - 1], &b = paths[s].p[idx];
				const Real da = dist2D(a, base), db = dist2D(b, base);
				const Real t = (da - r0) / (da - db + 0.001f);
				cross.x = a.x + (b.x - a.x) * t;
				cross.y = a.y + (b.y - a.y) * t;
			}
			cross.z = TheTerrainLogic->getGroundHeight(cross.x, cross.y);
			Int into = -1;
			for (Int k = 0; k < count; ++k)
				if (dist2D(list[k].m_cross, cross) < 120.0f)
					into = k;
			if (into >= 0)
			{
				priors[into] += 1.0f / numStarts;
				continue;
			}
			AIEntrance &e = list[count];
			e.m_cross = cross;
			geoFindChoke(paths[s], idx, e, sk.m_geoLookOut);
			priors[count] = 1.0f / numStarts;
			++count;
		}
	}

	if (count == 0)
		return TRUE;

	// Strongest first (equal shares keep their order).
	for (Int i = 1; i < count; ++i)
	{
		for (Int j = i; j > 0 && priors[j] > priors[j - 1]; --j)
		{
			AIEntrance t = list[j]; list[j] = list[j - 1]; list[j - 1] = t;
			Real p = priors[j]; priors[j] = priors[j - 1]; priors[j - 1] = p;
		}
	}
	if (count > GEO_MAX_ENTRANCES)
		count = GEO_MAX_ENTRANCES;
	Real total = 0.0f;
	for (Int i = 0; i < count; ++i)
		total += priors[i];
	m_numEntrances = count;
	for (Int i = 0; i < count; ++i)
	{
		m_entrances[i] = list[i];
		m_entrances[i].m_prior = total > 0.0f ? priors[i] / total : 1.0f / count;
		m_entrances[i].m_weight = m_entrances[i].m_prior;
		m_entrances[i].m_seen = 0.0f;
		m_entrances[i].m_defences = 0;
		// a place just inside the way in, on it: where the army waits
		const AIEntrance &e = m_entrances[i];
		Coord3D rally = base;
		Real dx = e.m_cross.x - base.x, dy = e.m_cross.y - base.y;
		const Real len = sqrtf(dx * dx + dy * dy) + 0.001f;
		dx /= len;
		dy /= len;
		Real radius = baseR + sk.m_geoRallyOut;
		if (m_geoWall)
		{
			// beside the chokepoint, not in it: an army that waits in a narrow gap blocks its own way out
			radius = dist2D(e.m_choke, base) + sk.m_geoRallyOffset;
			if (radius < 0.5f * baseR)
				radius = 0.5f * baseR;
		}
		Coord3D pick = base;
		for (Int tries = 0; tries < 12; ++tries)
		{
			pick.x = base.x + dx * radius;
			pick.y = base.y + dy * radius;
			if (groundOpen(pick))
				break;
			radius -= 15.0f;
			if (radius < 0.5f * baseR)
				break;
		}
		rally = pick;
		rally.z = TheTerrainLogic->getGroundHeight(rally.x, rally.y);
		m_entrances[i].m_rally = rally;
	}
	m_geoMain = 0;
	return TRUE;
}

/// The ways in, once; the pathfinder may not be ready in the first frames, so this is retried.
Bool AIStrategy::geoPrepare()
{
	if (m_geoReady)
		return TRUE;
	if (!geoOn())
		return FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextGeo)
		return FALSE;
	m_nextGeo = now + 5 * LOGICFRAMES_PER_SECOND;
	if (++m_geoTries > 6)
	{
		m_geoReady = TRUE;
		AI_TRACE("GEO: no way in could be worked out (the pathfinder gave no path); the fixed layout stays");
		return FALSE;
	}
	if (!geoCompute())
		return FALSE;
	m_geoReady = TRUE;

	Coord3D base;
	m_ai->getBaseCenter(&base);
	AI_TRACE("GEO: %s around the base (radius %.0f, perimeter %.0f): %d way(s) in", m_geoWall ? "terrain closes the perimeter" : "open ground",
		m_ai->m_baseRadius, m_geoRing, m_numEntrances);
	for (Int i = 0; i < m_numEntrances; ++i)
	{
		const AIEntrance &e = m_entrances[i];
		AI_TRACE("GEO: way in %d: crosses at (%.0f,%.0f) bearing %.0f deg; %s at (%.0f,%.0f) width %.0f; expected %.0f%% of the attacks; army waits at (%.0f,%.0f)",
			i + 1, e.m_cross.x, e.m_cross.y, atan2f(e.m_cross.y - base.y, e.m_cross.x - base.x) * 180.0f / PI,
			e.m_bridge ? "bridge" : (e.m_choked ? "chokepoint" : "open ground"), e.m_choke.x, e.m_choke.y, e.m_width,
			e.m_weight * 100.0f, e.m_rally.x, e.m_rally.y);
	}
	return TRUE;
}

// ---- where to put defences ------------------------------------------------------------------------------------------------------------
/// Collects the positions of defence structures that already stand (or were placed by this logic) for the spacing rule.
static Int collectDefencePositions(Player *player, Coord3D *out, Int maxCount)
{
	Int n = 0;
	for (Object *obj = TheGameLogic->getFirstObject(); obj && n < maxCount; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE) || obj->isEffectivelyDead())
			continue;
		if (isDefenceTemplate(obj->getTemplate()))
			out[n++] = *obj->getPosition();
	}
	return n;
}

/// A site for a defence structure: covers the chokepoint of one of the ways in (the one that has the fewest defences for its
/// share of the threat), on high ground, inside the chokepoint, near enough to the base to be looked after, away from other
/// defences.  False when there is no such site (or no way in is known); the caller keeps the original place then.
Bool AIStrategy::geoDefenceSite( const ThingTemplate *tmpl, Coord3D *pos, Real *angle )
{
	if (!geoOn() || m_ai->isFeatureOff(AIPlayer::AIF_GEOSITES) || !geoPrepare() || m_numEntrances == 0 || tmpl == nullptr)
		return FALSE;
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return FALSE;
	const AISkillSettings &sk = skill();
	const AICombatFigures *fig = AICombatModel::figures(tmpl);
	Real range = (fig && fig->m_range > 30.0f) ? fig->m_range : 200.0f;
	const Real baseR = m_ai->m_baseRadius;
	const Real reach = baseR + sk.m_geoReach;

	// The way in with the largest expected threat per defence (earlier ways win ties).
	Int pick = 0;
	Real bestRatio = -1.0f;
	for (Int i = 0; i < m_numEntrances; ++i)
	{
		const Real ratio = m_entrances[i].m_weight / (Real)(m_entrances[i].m_defences + 1);
		if (ratio > bestRatio + 0.0001f)
		{
			bestRatio = ratio;
			pick = i;
		}
	}
	AIEntrance &e = m_entrances[pick];

	// What to cover: the chokepoint, or the crossing when the chokepoint is too far out for a defence near the base.
	Coord3D target = e.m_choke;
	if (dist2D(target, base) > reach + 0.8f * range)
		target = e.m_cross;
	const Real targetHeight = TheTerrainLogic->getGroundHeight(target.x, target.y);

	Coord3D others[24];
	const Int numOthers = collectDefencePositions(m_player, others, 24);
	const Real radius = tmpl->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real spacing = 2.4f * radius + 20.0f;

	enum { RINGS = 3, DIRS = 16, CANDIDATES = RINGS * DIRS };
	Coord3D cand[CANDIDATES];
	Real score[CANDIDATES];
	Int num = 0;
	static const Real ringShare[RINGS] = { 0.40f, 0.60f, 0.80f };
	for (Int r = 0; r < RINGS; ++r)
	{
		for (Int d = 0; d < DIRS; ++d)
		{
			const Real a = 2.0f * PI * d / DIRS;
			Coord3D p = target;
			p.x += cosf(a) * range * ringShare[r];
			p.y += sinf(a) * range * ringShare[r];
			const Real fromBase = dist2D(p, base);
			if (fromBase > reach || fromBase < 0.45f * baseR)
				continue;
			if (!groundOpen(p))
				continue;
			Bool tooClose = FALSE;
			for (Int k = 0; k < numOthers && !tooClose; ++k)
				tooClose = dist2D(others[k], p) < spacing;
			for (Int k = 0; k < m_numGeoSites && !tooClose; ++k)
				tooClose = dist2D(m_geoSites[k], p) < spacing;
			if (tooClose)
				continue;
			p.z = TheTerrainLogic->getGroundHeight(p.x, p.y);
			Real s = 0.0f;
			Real rise = (p.z - targetHeight) / 10.0f;
			if (rise > 3.0f) rise = 3.0f;
			if (rise < -3.0f) rise = -3.0f;
			s += 6.0f * rise;											// high ground
			s += 10.0f * (1.0f - ringShare[r]);							// near the chokepoint covers it with more of the range
			if (fromBase < dist2D(target, base))
				s += 8.0f;												// inside the chokepoint: the base is behind it
			s -= 0.02f * fromBase;										// inside reach of the base
			cand[num] = p;
			score[num] = s;
			++num;
		}
	}

	// Best first; the first that may be built there.
	const Real placeAngle = tmpl->getPlacementViewAngle();
	for (Int tries = 0; tries < 12 && num > 0; ++tries)
	{
		Int best = 0;
		for (Int i = 1; i < num; ++i)
			if (score[i] > score[best])
				best = i;
		const Bool legal = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&cand[best], tmpl, placeAngle,
			BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player)
			&& reachableFrom(e.m_rally, cand[best]);
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback
		if (legal)
		{
			*pos = cand[best];
			*angle = placeAngle;
			++e.m_defences;
			if (m_numGeoSites < GEO_MAX_SITES)
				m_geoSites[m_numGeoSites++] = *pos;
			++m_geoPlaced;
			AI_TRACE("GEO: defence %s (range %.0f) at (%.0f,%.0f) height %.0f for way in %d (%s at (%.0f,%.0f), %d defence(s) there now)",
				tmpl->getName().str(), range, pos->x, pos->y, pos->z, pick + 1,
				e.m_bridge ? "bridge" : (e.m_choked ? "chokepoint" : "crossing"), target.x, target.y, e.m_defences);
			return TRUE;
		}
		score[best] = -1.0e9f;
		cand[best] = cand[num - 1];
		score[best] = score[num - 1];
		--num;
	}
	return FALSE;
}

/// Moves the defence structures of the build list that are not built yet to the ways in.
void AIStrategy::geoPlaceBuildList()
{
	if (!geoOn() || m_ai->isFeatureOff(AIPlayer::AIF_GEOSITES) || !geoPrepare() || m_numEntrances == 0)
		return;
	Int index = 0;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext(), ++index)
	{
		if (index >= 32 || (m_geoMoved & (1u << index)) != 0)
			continue;
		if (info->getTemplateName().isEmpty() || info->getObjectID() != INVALID_ID)
			continue;
		const ThingTemplate *tmpl = TheThingFactory->findTemplate(info->getTemplateName());
		if (!isDefenceTemplate(tmpl))
			continue;
		m_geoMoved |= (1u << index);
		const Coord3D was = *info->getLocation();
		Coord3D pos;
		Real angle;
		if (geoDefenceSite(tmpl, &pos, &angle))
		{
			info->setLocation(pos);
			++m_geoPlacedFixed;
			AI_TRACE("GEO: build list %s moved from (%.0f,%.0f) to (%.0f,%.0f)", tmpl->getName().str(), was.x, was.y, pos.x, pos.y);
		}
		else
			AI_TRACE("GEO: build list %s stays at (%.0f,%.0f): no site", tmpl->getName().str(), was.x, was.y);
	}
}

// ---- the main way in -------------------------------------------------------------------------------------------------------------
/// Where the army waits: just inside the way in that is expected to carry the most attacks.
Bool AIStrategy::geoRally( Coord3D *pos ) const
{
	if (!m_geoReady || m_numEntrances == 0 || m_geoMain < 0 || m_geoMain >= m_numEntrances || !geoOn() || m_ai->isFeatureOff(AIPlayer::AIF_GEORALLY))
		return FALSE;
	*pos = m_entrances[m_geoMain].m_rally;
	return TRUE;
}

/// Per-frame upkeep (cheap): retry the work out of the ways in, move build list defences, mix what the enemy model has seen
/// into the shares.
void AIStrategy::updateGeo()
{
	if (!geoOn())
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!m_geoReady)
	{
		if (geoPrepare())
			geoPlaceBuildList();
		return;
	}
	if (m_numEntrances == 0 || now < m_nextGeoWeights)
		return;
	m_nextGeoWeights = now + 3 * LOGICFRAMES_PER_SECOND;
	geoPlaceBuildList();

	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return;
	const Real baseR = m_ai->m_baseRadius;
	// what was seen of enemy ground forces on the approaches
	for (Int i = 0; i < m_numEntrances; ++i)
		m_entrances[i].m_seen *= 0.97f;
	Real total = 0.0f;
	for (Int c = 0; c < m_enemy.numContacts(); ++c)
	{
		const AIContact &ct = m_enemy.contacts()[c];
		if (ct.m_role > AIROLE_VEHICLE || now - ct.m_lastSeen > 6 * LOGICFRAMES_PER_SECOND)
			continue;
		const Real d = dist2D(ct.m_pos, base);
		if (d < baseR || d > baseR + 800.0f)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(ct.m_templateID));
		if (f == nullptr || !f->m_armed)
			continue;
		Int closest = -1;
		Real closestDist = 450.0f;
		for (Int i = 0; i < m_numEntrances; ++i)
		{
			const Real dd = dist2D(ct.m_pos, m_entrances[i].m_choke);
			if (dd < closestDist)
			{
				closestDist = dd;
				closest = i;
			}
		}
		if (closest >= 0)
			m_entrances[closest].m_seen += 0.1f * f->m_cost;
	}
	for (Int i = 0; i < m_numEntrances; ++i)
		total += m_entrances[i].m_seen;
	const Real mix = total / (total + 1500.0f) * 0.5f;		// at most half the weight comes from what was seen
	for (Int i = 0; i < m_numEntrances; ++i)
		m_entrances[i].m_weight = (1.0f - mix) * m_entrances[i].m_prior + (total > 0.0f ? mix * m_entrances[i].m_seen / total : 0.0f);
	// the main way in changes only when another one is clearly ahead
	Int top = m_geoMain;
	for (Int i = 0; i < m_numEntrances; ++i)
		if (m_entrances[i].m_weight > m_entrances[top].m_weight * 1.3f + 0.02f)
			top = i;
	if (top != m_geoMain)
	{
		m_geoMain = top;
		++m_geoRallyMoves;
		m_rallySet = FALSE;
		AI_TRACE("GEO: the main way in is now %d (shares %.0f%% / %.0f%%), the army waits at (%.0f,%.0f)", top + 1, m_entrances[top].m_weight * 100.0f,
			m_numEntrances > 1 ? m_entrances[top == 0 ? 1 : 0].m_weight * 100.0f : 0.0f, m_entrances[top].m_rally.x, m_entrances[top].m_rally.y);
	}
}
