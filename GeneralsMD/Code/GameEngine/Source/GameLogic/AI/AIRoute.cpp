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

// AIRoute.cpp
// Routes around static defences for the Expert computer player.
//
//  * AIPlanDetour: waypoints that keep a path out of a set of circles (the reach of the defences that have been
//    seen: their weapon range plus a margin).  Plain geometry on synchronised numbers, no library calls beyond sqrtf.
//  * Waves: before a wave goes, the straight way to its objective is checked against the seen defences.  A way that
//    crosses their reach gets up to three waypoints around them (the wave gathers at each one before it goes on).
//  * Breaching: defences that cover the objective itself cannot be avoided.  Units that out-range them (a longer
//    weapon than the defence, able to hurt structures) are sent ahead to destroy them from outside their reach, while
//    the wave waits at a staging point outside it; the wave goes in when the defences are down (or after a time limit).
//
// Only what the player has seen (AIEnemyModel) is used, and only template data (weapon ranges and damage).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/AIRoute.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static inline Real healthShare(Object *obj)
{
	BodyModuleInterface *body = obj->getBodyModule();
	if (body == nullptr || body->getMaxHealth() <= 0.0f)
		return 1.0f;
	const Real h = body->getHealth() / body->getMaxHealth();
	return h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
}

//-------------------------------------------------------------------------------------------------
// geometry
//-------------------------------------------------------------------------------------------------
/// Distance from p to the segment a-b; 't' receives the position of the closest point along it (0..1).
static Real distToSegment(const Coord3D &p, const Coord3D &a, const Coord3D &b, Real *t)
{
	const Real dx = b.x - a.x, dy = b.y - a.y;
	const Real len2 = dx * dx + dy * dy;
	Real u = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
	u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
	const Real cx = a.x + u * dx - p.x, cy = a.y + u * dy - p.y;
	if (t)
		*t = u;
	return sqrtf(cx * cx + cy * cy);
}

/// The first circle (smallest position along a-b) that the segment crosses, or -1.  Circles that contain an end of the segment are skipped.
static Int firstBlocker(const Coord3D &a, const Coord3D &b, const AIRouteCircle *circles, Int n, Real *at)
{
	Int found = -1;
	Real best = 2.0f;
	for (Int i = 0; i < n; ++i)
	{
		if (dist2D(circles[i].m_center, a) < circles[i].m_radius || dist2D(circles[i].m_center, b) < circles[i].m_radius)
			continue;
		Real t;
		if (distToSegment(circles[i].m_center, a, b, &t) < circles[i].m_radius && t < best)
		{
			best = t;
			found = i;
		}
	}
	if (at)
		*at = best;
	return found;
}

static Bool insideAny(const Coord3D &p, const AIRouteCircle *circles, Int n)
{
	for (Int i = 0; i < n; ++i)
		if (dist2D(circles[i].m_center, p) < circles[i].m_radius)
			return TRUE;
	return FALSE;
}

Real AIPathLength(const Coord3D &from, const Coord3D *via, Int numVia, const Coord3D &to)
{
	Real len = 0.0f;
	Coord3D prev = from;
	for (Int i = 0; i < numVia; ++i)
	{
		len += dist2D(prev, via[i]);
		prev = via[i];
	}
	return len + dist2D(prev, to);
}

/**
 * Waypoints from 'from' to 'to' that keep the legs out of the circles.  Returns the number of waypoints (0: the straight way
 * is clear), or -1 when no way within the limits was found.  A circle that contains 'from' or 'to' is not avoided (a
 * target inside the reach of a defence cannot be reached otherwise).
 *
 * A shortest path over a small visibility graph: the nodes are 'from', 'to' and eight points around every circle (just
 * outside it); an edge is a straight leg that crosses no circle.  The path may have at most maxWaypoints bends.
 */
Int AIPlanDetour(const Coord3D &from, const Coord3D &to, const AIRouteCircle *circles, Int numCircles, Coord3D *waypoints,
	Int maxWaypoints, Real maxDetourFactor, const Region3D *extent)
{
	const Real direct = dist2D(from, to);
	if (firstBlocker(from, to, circles, numCircles, nullptr) < 0)
		return 0;
	if (numCircles > AIROUTE_MAX_CIRCLES)
		numCircles = AIROUTE_MAX_CIRCLES;
	if (maxWaypoints > AIROUTE_MAX_POINTS)
		maxWaypoints = AIROUTE_MAX_POINTS;

	enum { MAX_NODES = 2 + 8 * AIROUTE_MAX_CIRCLES };
	static const Real cosines[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
	static const Real sines[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f };
	Coord3D node[MAX_NODES];
	Int count = 0;
	node[count++] = from;		// 0
	node[count++] = to;			// 1
	for (Int c = 0; c < numCircles; ++c)
	{
		for (Int k = 0; k < 8; ++k)
		{
			Coord3D p;
			p.x = circles[c].m_center.x + cosines[k] * circles[c].m_radius * 1.2f;
			p.y = circles[c].m_center.y + sines[k] * circles[c].m_radius * 1.2f;
			p.z = 0.0f;
			if (extent && (p.x < extent->lo.x + 40.0f || p.x > extent->hi.x - 40.0f || p.y < extent->lo.y + 40.0f || p.y > extent->hi.y - 40.0f))
				continue;
			if (insideAny(p, circles, numCircles))
				continue;
			node[count++] = p;
		}
	}

	// Layered shortest path: best[h][n] is the shortest length to node n with h legs.
	Real best[AIROUTE_MAX_POINTS + 2][MAX_NODES];
	Int from_[AIROUTE_MAX_POINTS + 2][MAX_NODES];
	for (Int h = 0; h < AIROUTE_MAX_POINTS + 2; ++h)
		for (Int n = 0; n < count; ++n)
		{
			best[h][n] = 1.0e30f;
			from_[h][n] = -1;
		}
	best[0][0] = 0.0f;
	// Edge validity is worked out once per pair when first needed (0: unknown, 1: clear, 2: blocked).
	static unsigned char edge[MAX_NODES][MAX_NODES];
	for (Int i = 0; i < count; ++i)
		for (Int j = 0; j < count; ++j)
			edge[i][j] = 0;

	const Int maxLegs = maxWaypoints + 1;
	for (Int h = 0; h < maxLegs; ++h)
	{
		for (Int a = 0; a < count; ++a)
		{
			if (best[h][a] >= 1.0e29f || a == 1)
				continue;
			for (Int b = 1; b < count; ++b)
			{
				if (b == a || b == 0)
					continue;
				if (edge[a][b] == 0)
				{
					const Bool clear = firstBlocker(node[a], node[b], circles, numCircles, nullptr) < 0;
					edge[a][b] = edge[b][a] = clear ? 1 : 2;
				}
				if (edge[a][b] != 1)
					continue;
				const Real len = best[h][a] + dist2D(node[a], node[b]);
				if (len < best[h + 1][b])
				{
					best[h + 1][b] = len;
					from_[h + 1][b] = a;
				}
			}
		}
	}

	// The best number of legs to reach 'to'.
	Int bestLegs = -1;
	Real bestLen = 1.0e30f;
	for (Int h = 1; h <= maxLegs; ++h)
	{
		if (best[h][1] < bestLen)
		{
			bestLen = best[h][1];
			bestLegs = h;
		}
	}
	if (bestLegs < 0 || (direct > 0.0f && bestLen > maxDetourFactor * direct))
		return -1;

	// Walk back: the nodes between 'to' and 'from' are the waypoints.
	Int chain[AIROUTE_MAX_POINTS + 2];
	Int n = 1;
	for (Int h = bestLegs; h >= 1; --h)
	{
		chain[h] = n;
		n = from_[h][n];
	}
	Int out = 0;
	for (Int h = 1; h < bestLegs; ++h)
		waypoints[out++] = node[chain[h]];
	return out;
}

//-------------------------------------------------------------------------------------------------
// waves
//-------------------------------------------------------------------------------------------------
Bool AIStrategy::routeOn() const
{
	return (skill().m_useRoute || m_ai->isFeatureForced(AIPlayer::AIF_ROUTE)) && !m_ai->isFeatureOff(AIPlayer::AIF_ROUTE);
}

/// The reach of the seen defences that shoot ground units (structures of the defence role), as circles to stay out of.
Int AIStrategy::collectDefenceCircles( AIRouteCircle *circles, Int maxCircles, ObjectID *ids ) const
{
	const Real margin = skill().m_routeMargin;
	Int n = 0;
	for (Int i = 0; i < m_enemy.numContacts() && n < maxCircles; ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role != AIROLE_DEFENCE)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || !f->m_canHitGround || f->m_groundRange <= 0.0f)
			continue;
		circles[n].m_center = c.m_pos;
		circles[n].m_radius = f->m_groundRange + margin;	// how far it reaches our ground army
		if (ids)
			ids[n] = c.m_id;
		++n;
	}
	return n;
}

/// Is a way for the wave planned and not yet finished?
Bool AIStrategy::routeActive() const
{
	return m_routeIdx < m_routeLen;
}

/**
 * Called when a wave is about to go: plans the way from 'from' to the objective around the seen defences, or the
 * breach of defences that cover the objective.  Fills m_route (the waypoints before the objective).
 */
void AIStrategy::planWaveRoute( const Coord3D &from, const Coord3D &objective )
{
	m_routeLen = 0;
	m_routeIdx = 0;
	m_routeLegStart = 0;
	m_routeArrived = 0;
	m_breachHold = FALSE;
	if (!routeOn())
		return;

	AIRouteCircle circles[AIROUTE_MAX_CIRCLES];
	ObjectID ids[AIROUTE_MAX_CIRCLES];
	const Int n = collectDefenceCircles(circles, AIROUTE_MAX_CIRCLES, ids);
	if (n == 0)
		return;

	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	Coord3D via[AIROUTE_MAX_POINTS];
	const Int count = AIPlanDetour(from, objective, circles, n, via, AIROUTE_MAX_POINTS, skill().m_routeMaxDetour, &extent);
	if (count > 0)
	{
		for (Int i = 0; i < count; ++i)
		{
			m_route[i] = via[i];
			m_route[i].z = TheTerrainLogic->getGroundHeight(via[i].x, via[i].y);
		}
		m_routeLen = count;
		++m_routesPlanned;
		const Real direct = dist2D(from, objective);
		AI_TRACE("WAVE route: %d waypoint(s) around %d known defences, %.0f%% longer; first (%.0f,%.0f)", count, n,
			direct > 0.0f ? 100.0f * (AIPathLength(from, via, count, objective) / direct - 1.0f) : 0.0f, via[0].x, via[0].y);
	}
	else if (count < 0)
	{
		AI_TRACE("WAVE route: no way around the %d known defences within the limits, going straight", n);
	}

	// Defences that cover the objective itself cannot be avoided: units that out-range them go first.
	startBreach(m_routeLen > 0 ? m_route[m_routeLen - 1] : from, objective, circles, ids, n);
}

/// The wave has reached its current waypoint: on to the next, or to the objective.
void AIStrategy::advanceWaveRoute( const Coord3D &waveCenter )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	updateBreach();
	if (!routeActive())
		return;
	if (m_routeLegStart == 0)
		m_routeLegStart = now;
	const Coord3D &wp = m_route[m_routeIdx];
	const Bool atLast = (m_routeIdx == m_routeLen - 1);
	if (atLast && m_breachHold)
	{
		// Waiting outside the defences while the breachers work.
		if (now - m_routeLegStart > secondsToFrames(skill().m_breachHoldSeconds) + 20 * LOGICFRAMES_PER_SECOND)
			m_breachHold = FALSE;
		else
			return;
	}

	// Together at the waypoint: the center of the wave is there, or the first teams have waited long enough.
	Real closeValue = 0.0f, total = 0.0f;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		const AITeamRecord &rec = m_teams[i];
		if (!rec.m_inWave || rec.m_mode != AITEAM_ATTACKING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		const Coord3D *p = team ? team->getEstimateTeamPosition() : nullptr;
		if (p == nullptr)
			continue;
		const Real v = teamValue(team);
		total += v;
		if (dist2D(*p, wp) < 220.0f)
			closeValue += v;
	}
	if (total <= 0.0f)
		return;
	if (closeValue < 0.7f * total && dist2D(waveCenter, wp) > 150.0f && now - m_routeLegStart < 90 * LOGICFRAMES_PER_SECOND)
	{
		if (closeValue > 0.0f && m_routeArrived == 0)
			m_routeArrived = now;
		if (m_routeArrived == 0 || now - m_routeArrived < 15 * LOGICFRAMES_PER_SECOND)
			return;
	}

	++m_routeIdx;
	m_routeLegStart = now;
	m_routeArrived = 0;
	const Coord3D next = routeActive() ? m_route[m_routeIdx] : m_waveObjective;
	AI_TRACE("WAVE passes waypoint %d of %d: on to (%.0f,%.0f)", m_routeIdx, m_routeLen, next.x, next.y);
	for (Int i = 0; i < m_numTeams; ++i)
	{
		AITeamRecord &rec = m_teams[i];
		if (!rec.m_inWave || rec.m_mode != AITEAM_ATTACKING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr)
			continue;
		rec.m_target = next;
		rec.m_orderFrame = now;
		orderTeamAttackMove(team, &next);
	}
}

/// A team of the wave that stands at the current waypoint waits there for the others (evaluateTeam).
Bool AIStrategy::routeHolds( const AITeamRecord *rec, const Coord3D &teamCenter ) const
{
	return routeActive() && rec->m_inWave && rec->m_mode == AITEAM_ATTACKING && m_armyState == ARMY_ATTACK &&
		dist2D(teamCenter, m_route[m_routeIdx]) < 160.0f;
}

//-------------------------------------------------------------------------------------------------
// breaching
//-------------------------------------------------------------------------------------------------
Bool AIStrategy::isBreacher( ObjectID id ) const
{
	for (Int i = 0; i < m_numBreachers; ++i)
		if (m_breachers[i] == id)
			return TRUE;
	return FALSE;
}

/**
 * Defences that cover the objective (their reach contains it) and are out-ranged by some of our units: those units go
 * to destroy them from outside their reach, the wave waits at a staging point.  Nothing happens when there is no unit
 * that out-ranges them, or when the defences are not within our sight (the objective is the place to be fought anyway).
 */
void AIStrategy::startBreach( const Coord3D &from, const Coord3D &objective, const AIRouteCircle *circles, const ObjectID *ids, Int numCircles )
{
	m_numBreachers = 0;
	m_numBreachTargets = 0;
	m_breachHold = FALSE;
	const AISkillSettings &sk = skill();

	// The defences whose reach holds the objective.
	Real maxReach = 0.0f;
	for (Int i = 0; i < numCircles && m_numBreachTargets < MAX_BREACH_TARGETS; ++i)
	{
		if (dist2D(circles[i].m_center, objective) >= circles[i].m_radius)
			continue;
		m_breachTargets[m_numBreachTargets++] = ids[i];
		if (circles[i].m_radius > maxReach)
			maxReach = circles[i].m_radius;
	}
	if (m_numBreachTargets == 0)
		return;
	// The longest range among the defences is what a breacher must exceed.
	Real defenceRange = 0.0f;
	for (Int i = 0; i < m_numBreachTargets; ++i)
	{
		for (Int k = 0; k < numCircles; ++k)
			if (ids[k] == m_breachTargets[i] && circles[k].m_radius - sk.m_routeMargin > defenceRange)
				defenceRange = circles[k].m_radius - sk.m_routeMargin;
	}

	// Our units that out-range them and can hurt a structure.
	const AICombatFigures *structure = nullptr;
	for (Int i = 0; i < m_enemy.numContacts() && structure == nullptr; ++i)
		if (m_enemy.contacts()[i].m_id == m_breachTargets[0])
			structure = AICombatModel::figures(TheThingFactory->findByTemplateID(m_enemy.contacts()[i].m_templateID));
	Real ranges[MAX_BREACHERS];
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr || m_teams[i].m_mode == AITEAM_RETREATING)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == nullptr || isDetached(obj->getID()))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || f->m_structure || f->m_airborne || f->m_speed <= 0.0f || !f->m_canHitGround)
				continue;
			if (f->m_groundRange < sk.m_breachRangeFactor * defenceRange)
				continue;
			// Only units with the wave: one that would walk alone across the map to its firing spot is lost on the way.
			if (dist2D(*obj->getPosition(), from) > 600.0f)
				continue;
			if (structure && AICombatModel::damagePerSecond(f, structure) <= 0.0f)
				continue;
			Int pos = m_numBreachers;
			while (pos > 0 && ranges[pos - 1] < f->m_groundRange)
				--pos;
			if (pos >= MAX_BREACHERS)
				continue;
			const Int last = m_numBreachers < MAX_BREACHERS ? m_numBreachers : MAX_BREACHERS - 1;
			for (Int k = last; k > pos; --k)
			{
				m_breachers[k] = m_breachers[k - 1];
				ranges[k] = ranges[k - 1];
			}
			m_breachers[pos] = obj->getID();
			ranges[pos] = f->m_groundRange;
			if (m_numBreachers < MAX_BREACHERS)
				++m_numBreachers;
		}
	}
	if (m_numBreachers == 0)
	{
		m_numBreachTargets = 0;
		AI_TRACE("BREACH: %d defence(s) cover the objective (reach %.0f) and no unit out-ranges them", numCircles, maxReach);
		return;
	}

	// Is it worth it?  The breachers must be able to bring the defences down within the hold time, and the troops seen around the
	// objective must not be much stronger than the breachers (they would only be lost; the wave then fights it out as it is).
	{
		Real partyValue = 0.0f;
		for (Int i = 0; i < m_numBreachers; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
			partyValue += AICombatModel::figures(o->getTemplate())->m_cost * healthShare(o);
		}
		Real seconds = 0.0f;
		for (Int t = 0; t < m_numBreachTargets; ++t)
		{
			for (Int i = 0; i < m_enemy.numContacts(); ++i)
			{
				const AIContact &c = m_enemy.contacts()[i];
				if (c.m_id != m_breachTargets[t])
					continue;
				const AICombatFigures *tf = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
				Real dps = 0.0f;
				for (Int b = 0; b < m_numBreachers; ++b)
				{
					Object *o = TheGameLogic->findObjectByID(m_breachers[b]);
					dps += AICombatModel::damagePerSecond(AICombatModel::figures(o->getTemplate()), tf);
				}
				seconds += dps > 0.0f ? tf->m_maxHealth * (0.25f + 0.75f * c.m_healthPct / 100.0f) / dps : 1.0e6f;
			}
		}
		const UnsignedInt now = TheGameLogic->getFrame();
		Real troops = 0.0f;
		for (Int i = 0; i < m_enemy.numContacts(); ++i)
		{
			const AIContact &c = m_enemy.contacts()[i];
			if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 12 * LOGICFRAMES_PER_SECOND || dist2D(c.m_pos, objective) > 450.0f)
				continue;
			const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
			if (f && f->m_armed && f->m_canHitGround)
				troops += f->m_cost;
		}
		if (seconds > 0.75f * sk.m_breachHoldSeconds || troops > 0.5f * partyValue)
		{
			AI_TRACE("BREACH skipped: %d breacher(s) (%.0f) would need %.0f s for the defences, %.0f of troops around the objective", m_numBreachers, partyValue, seconds, troops);
			m_numBreachers = 0;
			m_numBreachTargets = 0;
			return;
		}
	}

	// The staging point: outside the reach of the defences, on the way from the army.
	Coord3D stage;
	const Real dx = from.x - objective.x, dy = from.y - objective.y;
	const Real len = sqrtf(dx * dx + dy * dy);
	if (len < 1.0f)
		return;
	stage.x = objective.x + dx / len * (maxReach + 60.0f);
	stage.y = objective.y + dy / len * (maxReach + 60.0f);
	stage.z = TheTerrainLogic->getGroundHeight(stage.x, stage.y);
	if (m_routeLen < AIROUTE_MAX_POINTS)
		m_route[m_routeLen++] = stage;
	else
		m_route[m_routeLen - 1] = stage;
	m_breachHold = TRUE;
	m_breachFallback = FALSE;
	m_breachStart = TheGameLogic->getFrame();
	m_breachInRange = 0;
	m_breachStage = stage;
	++m_breachesStarted;
	AI_TRACE("BREACH: %d unit(s) out-range %d defence(s) that cover (%.0f,%.0f); the wave waits at (%.0f,%.0f)", m_numBreachers, m_numBreachTargets, objective.x, objective.y, stage.x, stage.y);

	// Send them: to the edge of their range, attack the nearest defence.
	for (Int i = 0; i < m_numBreachers; ++i)
		orderBreacher(m_breachers[i]);
}

/**
 * Sends one breacher against the nearest defence that is still standing: the attack order of the engine takes it the shortest way into
 * range (it out-ranges the defence, so it fires from outside its reach).  A defence out of sight is shelled where it was seen, as a
 * player does with artillery.
 */
void AIStrategy::orderBreacher( ObjectID id )
{
	Object *unit = TheGameLogic->findObjectByID(id);
	if (unit == nullptr || unit->getAI() == nullptr)
		return;
	const AIContact *best = nullptr;
	Real bestD = 0.0f;
	for (Int t = 0; t < m_numBreachTargets; ++t)
	{
		for (Int i = 0; i < m_enemy.numContacts(); ++i)
		{
			const AIContact &c = m_enemy.contacts()[i];
			if (c.m_id != m_breachTargets[t])
				continue;
			const Real d = dist2D(*unit->getPosition(), c.m_pos);
			if (best == nullptr || d < bestD)
			{
				best = &c;
				bestD = d;
			}
		}
	}
	if (best == nullptr)
		return;
	Object *target = TheGameLogic->findObjectByID(best->m_id);
	if (target && !target->isEffectivelyDead() && enemyCanSee(target))
		unit->getAI()->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
	else
	{
		Coord3D at = best->m_pos;
		unit->getAI()->aiAttackPosition(&at, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
	}
}

/// The breachers at work: keep them on a standing defence, finish when the defences are down, the breachers are lost, or time is up.
void AIStrategy::updateBreach()
{
	if (m_numBreachers == 0)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextBreachCheck)
		return;
	m_nextBreachCheck = now + LOGICFRAMES_PER_SECOND;

	// Breachers that live.
	Int out = 0;
	for (Int i = 0; i < m_numBreachers; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
		if (o && !o->isEffectivelyDead())
			m_breachers[out++] = m_breachers[i];
	}
	m_numBreachers = out;

	// Defences that still stand (as far as we know: a contact we can see is gone when its object is).
	Int standing = 0;
	for (Int t = 0; t < m_numBreachTargets; ++t)
	{
		for (Int i = 0; i < m_enemy.numContacts(); ++i)
		{
			if (m_enemy.contacts()[i].m_id == m_breachTargets[t])
			{
				++standing;
				break;
			}
		}
	}

	// The hold time counts from the moment the first breacher is within range of a target (slow artillery takes long to get there).
	if (m_breachInRange == 0)
	{
		for (Int i = 0; i < m_numBreachers && m_breachInRange == 0; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
			const AICombatFigures *f = o ? AICombatModel::figures(o->getTemplate()) : nullptr;
			for (Int t = 0; f && t < m_numBreachTargets && m_breachInRange == 0; ++t)
				for (Int k = 0; k < m_enemy.numContacts(); ++k)
					if (m_enemy.contacts()[k].m_id == m_breachTargets[t] && dist2D(*o->getPosition(), m_enemy.contacts()[k].m_pos) < f->m_groundRange + 60.0f)
					{
						m_breachInRange = now;
						AI_TRACE("BREACH: the first breacher is within range after %u s", (now - m_breachStart) / LOGICFRAMES_PER_SECOND);
						break;
					}
		}
	}

	const char *why = nullptr;
	if (m_armyState != ARMY_ATTACK)
		why = "the wave is over";
	else if (m_numBreachers == 0)
		why = "the breachers are lost";
	else if (standing == 0)
		why = "the defences are down";
	else if (m_breachInRange != 0 && now - m_breachInRange > secondsToFrames(skill().m_breachHoldSeconds))
		why = "time is up";
	else if (now - m_breachStart > 3 * secondsToFrames(skill().m_breachHoldSeconds))
		why = "time is up";
	if (why == nullptr)
	{
		if (m_trace && (now % (5 * LOGICFRAMES_PER_SECOND)) < LOGICFRAMES_PER_SECOND)
		{
			Object *lead = TheGameLogic->findObjectByID(m_breachers[0]);
			Real hp = 0.0f;
			for (Int i = 0; i < m_numBreachers; ++i)
				hp += healthShare(TheGameLogic->findObjectByID(m_breachers[i]));
			AI_TRACE("BREACH: %d breachers (health %.0f%%) at (%.0f,%.0f), %d defence(s) standing, enemy armed value within 300: %.0f", m_numBreachers, 100.0f * hp / m_numBreachers,
				lead->getPosition()->x, lead->getPosition()->y, standing, armyValueNear(lead->getPosition(), 300.0f, TRUE));
		}
		// Enemy troops that come for the breachers: they fall back to the wave's staging point and let it fight, and go
		// back to work when it is quiet again.
		Real partyValue = 0.0f, enemyNear = 0.0f;
		for (Int i = 0; i < m_numBreachers; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
			const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
			partyValue += f ? f->m_cost * healthShare(o) : 0.0f;
			const Real closeValue = armyValueNear(o->getPosition(), 230.0f, TRUE);
			if (closeValue > enemyNear)
				enemyNear = closeValue;
		}
		if (!m_breachFallback && enemyNear > 0.7f * partyValue)
		{
			m_breachFallback = TRUE;
			AI_TRACE("BREACH: enemy troops (%.0f) come for the breachers (%.0f): they fall back to the wave", enemyNear, partyValue);
			for (Int i = 0; i < m_numBreachers; ++i)
			{
				Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
				if (o && o->getAI())
					o->getAI()->aiMoveToPosition(&m_breachStage, CMD_FROM_AI);
			}
		}
		else if (m_breachFallback && enemyNear < 0.3f * partyValue)
		{
			m_breachFallback = FALSE;
			for (Int i = 0; i < m_numBreachers; ++i)
				orderBreacher(m_breachers[i]);
		}
		if (m_breachFallback)
			return;
		// Keep each breacher on a defence (a new order only when it has nothing to do).
		for (Int i = 0; i < m_numBreachers; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
			if (o && o->getAI() && o->getAI()->isIdle())
				orderBreacher(m_breachers[i]);
		}
		return;
	}
	AI_TRACE("BREACH over: %s (%d breachers left, %d of %d defences standing)", why, m_numBreachers, standing, m_numBreachTargets);
	if (standing == 0)
		++m_breachKills;
	m_breachHold = FALSE;
	for (Int i = 0; i < m_numBreachers; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_breachers[i]);
		if (o && o->getAI())
			o->getAI()->aiAttackMoveToPosition(&m_waveObjective, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
	}
	m_numBreachers = 0;
	m_numBreachTargets = 0;
}
