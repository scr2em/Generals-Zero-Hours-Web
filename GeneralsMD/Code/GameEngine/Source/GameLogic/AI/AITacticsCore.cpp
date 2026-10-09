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

// AITacticsCore.cpp
// See AITacticsCore.h.  The code here was part of the Expert computer player (AITactics.cpp, AI.cpp); it moved
// here unchanged in behaviour so that the stances of the player assists use the same rules.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/AITacticsCore.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static inline Real dist2D( const Coord3D &a, const Coord3D &b )
{
	return sqrtf( (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) );
}

namespace AITactics
{

//-------------------------------------------------------------------------------------------------
Params paramsOf( const AISkillSettings &sk )
{
	Params p;
	p.m_kiteMinReloadSeconds = sk.m_kiteMinReloadSeconds;
	p.m_kiteRangeFactor = sk.m_kiteRangeFactor;
	p.m_kiteSpeedFactor = sk.m_kiteSpeedFactor;
	p.m_kiteGroupRadius = sk.m_kiteGroupRadius;
	p.m_kiteMinThreat = sk.m_kiteMinThreat;
	p.m_splashRadiusThreshold = sk.m_splashRadiusThreshold;
	p.m_maxSpacing = sk.m_maxSpacing;
	p.m_splitWindowSeconds = sk.m_splitWindowSeconds;
	return p;
}

static inline UnsignedInt secondsToFrames( Real seconds )
{
	Int f = (Int)(seconds * LOGICFRAMES_PER_SECOND + 0.5f);
	return f < 1 ? 1 : (UnsignedInt)f;
}

//-------------------------------------------------------------------------------------------------
// kiting
//-------------------------------------------------------------------------------------------------
KiteResult planKite( const Params &sk, Object *unit, Object *victim, const Coord3D *home, const ContactList &contacts, Coord3D *to, UnsignedInt *until )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	AIUpdateInterface *ai = unit->getAI();
	if (ai == nullptr)
		return KITE_NONE;

	const AICombatFigures *mf = AICombatModel::figures(unit->getTemplate());
	const AICombatFigures *vf = AICombatModel::figures(victim->getTemplate());
	if (mf == nullptr || vf == nullptr)
		return KITE_NONE;
	if (mf->m_structure || mf->m_airborne || mf->m_speed <= 0.0f || mf->m_range < 60.0f)
		return KITE_NONE;
	// Only mobile fighters that can hurt us chase us; a building or a harmless unit gives no reason to move.
	if (vf->m_structure || vf->m_airborne || vf->m_speed <= 0.0f || !vf->m_armed || AICombatModel::killRate(vf, mf) < sk.m_kiteMinThreat)
		return KITE_NONE;

	// Can we keep away from it?
	const Real ratio = mf->m_speed / vf->m_speed;
	const Bool outranges = ratio >= 1.0f && mf->m_range >= sk.m_kiteRangeFactor * vf->m_range;
	const Bool outruns = ratio >= sk.m_kiteSpeedFactor && mf->m_range >= 0.95f * vf->m_range;
	if (!outranges && !outruns)
		return KITE_NONE;

	// The weapon must be waiting for a while.
	Weapon *weapon = unit->getCurrentWeapon();
	if (weapon == nullptr)
		return KITE_NONE;
	const WeaponStatus status = weapon->getStatus();
	if (status != BETWEEN_FIRING_SHOTS && status != RELOADING_CLIP)
		return KITE_NONE;
	const UnsignedInt next = weapon->getPossibleNextShotFrame();
	if (next <= now + secondsToFrames(sk.m_kiteMinReloadSeconds))
		return KITE_NONE;
	const Real gapSeconds = (Real)(next - now) / LOGICFRAMES_PER_SECOND;

	// Geometry: back to the edge of our range, directly away from the enemy.
	const Coord3D &up = *unit->getPosition();
	const Coord3D &vp = *victim->getPosition();
	const Real dx = up.x - vp.x, dy = up.y - vp.y;
	const Real dist = sqrtf(dx * dx + dy * dy);
	if (dist < 1.0f)
		return KITE_NONE;
	const Real myRange = weapon->getAttackRange(unit);
	const Real want = myRange - 10.0f;
	if (dist >= want - 12.0f)
		return KITE_NONE;		// already at the edge of the range
	// The enemy could not reach us during the wait anyway: stay and keep the aim.
	if (dist > vf->m_range + vf->m_speed * gapSeconds * 0.8f + 20.0f)
		return KITE_NONE;

	Real step = want - dist;
	const Real maxStep = mf->m_speed * gapSeconds * 0.45f;		// there and back within the wait
	if (step > maxStep)
		step = maxStep;
	if (step < 12.0f)
		return KITE_NONE;
	to->x = up.x + dx / dist * step;
	to->y = up.y + dy / dist * step;
	to->z = TheTerrainLogic->getGroundHeight(to->x, to->y);

	// Faster enemies about: kiting would only get the unit run down.
	Contact c;
	for (Int i = 0; i < contacts.count(); ++i)
	{
		if (!contacts.get(i, &c) || c.m_fig == nullptr)
			continue;
		if (c.m_fig->m_role > AIROLE_AIRCRAFT || c.m_age > 3 * LOGICFRAMES_PER_SECOND || dist2D(c.m_pos, up) > myRange + 150.0f)
			continue;
		const AICombatFigures *cf = c.m_fig;
		if (cf->m_armed && !cf->m_airborne && cf->m_speed > mf->m_speed * 1.02f && AICombatModel::damagePerSecond(cf, mf) > 0.0f)
			return KITE_REJECT_FAST;
	}

	// Room behind us: on the map, passable, not under the guns of other enemies, and not away from our group.
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	if (to->x < extent.lo.x + 40.0f || to->x > extent.hi.x - 40.0f || to->y < extent.lo.y + 40.0f || to->y > extent.hi.y - 40.0f ||
			!ai->isValidLocomotorPosition(to))
		return KITE_REJECT_CORNER;
	for (Int i = 0; i < contacts.count(); ++i)
	{
		if (!contacts.get(i, &c) || c.m_fig == nullptr)
			continue;
		if (c.m_id == victim->getID() || c.m_fig->m_role > AIROLE_DEFENCE || (c.m_fig->m_role < AIROLE_DEFENCE && c.m_age > 3 * LOGICFRAMES_PER_SECOND))
			continue;
		const AICombatFigures *cf = c.m_fig;
		if (cf->m_armed && cf->m_canHitGround && dist2D(c.m_pos, *to) < cf->m_range + 20.0f && dist2D(c.m_pos, up) >= cf->m_range + 20.0f)
			return KITE_REJECT_CORNER;
	}
	if (home && dist2D(*home, *to) > sk.m_kiteGroupRadius)
		return KITE_NONE;

	const UnsignedInt stepFrames = (UnsignedInt)(step / mf->m_speed * LOGICFRAMES_PER_SECOND) + 15;
	*until = now + (stepFrames < next - now ? stepFrames : next - now);
	return KITE_YES;
}

//-------------------------------------------------------------------------------------------------
// spreading out
//-------------------------------------------------------------------------------------------------
Real spacingForBlast( const Params &sk, Real blast )
{
	if (blast <= 0.0f)
		return 0.0f;
	Real spacing = blast < 25.0f ? 25.0f : blast;
	if (spacing > sk.m_maxSpacing)
		spacing = sk.m_maxSpacing;
	return spacing;
}

Bool planSpread( const Params &sk, Real spacing, Object *unit, Object *victim, const Coord3D *home, Coord3D *to, UnsignedInt *until )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	AIUpdateInterface *ai = unit->getAI();
	const AICombatFigures *mf = AICombatModel::figures(unit->getTemplate());
	if (ai == nullptr || mf == nullptr || mf->m_structure || mf->m_airborne || mf->m_speed <= 0.0f)
		return FALSE;

	const Coord3D &up = *unit->getPosition();
	Real range = 0.0f, reloadSeconds = 0.0f;
	if (victim)
	{
		Weapon *weapon = unit->getCurrentWeapon();
		if (weapon == nullptr)
			return FALSE;
		const WeaponStatus status = weapon->getStatus();
		if (status != BETWEEN_FIRING_SHOTS && status != RELOADING_CLIP)
			return FALSE;
		const UnsignedInt next = weapon->getPossibleNextShotFrame();
		if (next <= now + secondsToFrames(sk.m_kiteMinReloadSeconds))
			return FALSE;
		reloadSeconds = (Real)(next - now) / LOGICFRAMES_PER_SECOND;
		range = weapon->getAttackRange(unit) - 5.0f;
	}

	// Our units around: push away from the ones that are closer than the spacing.
	enum { MAX_NEIGHBOURS = 12 };
	Coord3D neighbours[MAX_NEIGHBOURS];
	Int numNear = 0;
	Real pushX = 0.0f, pushY = 0.0f, closest = spacing;
	{
		PartitionFilterAlive alive;
		PartitionFilterRelationship friends(unit, PartitionFilterRelationship::ALLOW_ALLIES);
		PartitionFilter *filters[] = { &friends, &alive, nullptr };
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&up, spacing, FROM_CENTER_2D, filters);
		MemoryPoolObjectHolder hold(iter);
		for (Object *o = iter->first(); o && numNear < MAX_NEIGHBOURS; o = iter->next())
		{
			if (o == unit)
				continue;
			const AICombatFigures *of = AICombatModel::figures(o->getTemplate());
			if (of == nullptr || of->m_structure || of->m_airborne || !of->m_armed || o->isContained())
				continue;
			const Real dx = up.x - o->getPosition()->x, dy = up.y - o->getPosition()->y;
			const Real d = sqrtf(dx * dx + dy * dy);
			neighbours[numNear++] = *o->getPosition();
			if (d < 0.75f * spacing)
			{
				const Real w = (0.75f * spacing - d) / (d > 1.0f ? d : 1.0f);
				pushX += dx * w;
				pushY += dy * w;
				if (d < closest)
					closest = d;
			}
		}
	}
	if (closest >= 0.75f * spacing)
		return FALSE;
	Real len = sqrtf(pushX * pushX + pushY * pushY);
	if (len < 0.001f)
	{
		// Exactly on top of each other: along x.
		pushX = 1.0f;
		pushY = 0.0f;
		len = 1.0f;
	}
	pushX /= len;
	pushY /= len;

	Real step = spacing - closest + 8.0f;
	if (step < 10.0f)
		step = 10.0f;
	if (step > spacing)
		step = spacing;
	if (victim)
	{
		const Real maxStep = mf->m_speed * reloadSeconds * 0.45f;
		if (step > maxStep)
			step = maxStep;
		if (step < 8.0f)
			return FALSE;
	}

	// Try the direction away from the others, then turned aside by fixed angles (no library calls).
	static const Real cosines[5] = { 1.0f, 0.6428f, 0.6428f, -0.1736f, -0.1736f };
	static const Real sines[5] = { 0.0f, 0.7660f, -0.7660f, 0.9848f, -0.9848f };
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	for (Int a = 0; a < 5; ++a)
	{
		const Real dx = pushX * cosines[a] - pushY * sines[a];
		const Real dy = pushX * sines[a] + pushY * cosines[a];
		Coord3D c;
		c.x = up.x + dx * step;
		c.y = up.y + dy * step;
		c.z = TheTerrainLogic->getGroundHeight(c.x, c.y);
		if (c.x < extent.lo.x + 40.0f || c.x > extent.hi.x - 40.0f || c.y < extent.lo.y + 40.0f || c.y > extent.hi.y - 40.0f)
			continue;
		if (home && dist2D(*home, c) > sk.m_kiteGroupRadius)
			continue;
		if (victim && dist2D(c, *victim->getPosition()) > range)
			continue;
		Bool crowded = FALSE;
		for (Int n = 0; n < numNear && !crowded; ++n)
			crowded = dist2D(neighbours[n], c) < 0.6f * spacing;
		if (crowded || !ai->isValidLocomotorPosition(&c))
			continue;
		*to = c;
		const UnsignedInt frames = (UnsignedInt)(step / mf->m_speed * LOGICFRAMES_PER_SECOND) + 15;
		*until = now + frames;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// target selection
//-------------------------------------------------------------------------------------------------
void Mates::add( const AICombatFigures *f )
{
	if (m_num < MAX)
	{
		m_fig[m_num++] = f;
		m_cost += f->m_cost;
	}
}

void scoreTarget( const AICombatFigures *mine, const AICombatFigures *f, Real health, Real dist, Real range, Bool threatFirst, const Mates &mates, TargetScore *out )
{
	// What does not depend on the kind of rules.
	Real common = 40.0f * (1.0f - health);																	// finish what is hurt
	const Real dps = AICombatModel::damagePerSecond(mine, f);
	if (dps > 0.0f)
		common += 25.0f / (1.0f + (health * f->m_maxHealth / dps) / 3.0f);		// quick kills
	if (dist <= mine->m_range)
		common += 20.0f;																											// in reach right now
	common -= 40.0f * (dist >= range ? 1.0f : dist / range);								// near is better

	// The original rule: it can hurt this unit: first.
	Real scoreA = common;
	const Real threat = AICombatModel::killRate(f, mine);
	if (threat > 0.0f)
		scoreA += 60.0f + (threat * 600.0f > 30.0f ? 30.0f : threat * 600.0f);
	else if (f->m_armed)
		scoreA += 25.0f;
	else if (!f->m_structure)
		scoreA += 15.0f;
	else
		scoreA += 5.0f;

	Real scoreB = scoreA;
	Int kind = 0;
	if (threatFirst)
	{
		// Threat: the share of the value of our units around here that it destroys every second.
		Real rate = 0.0f;
		if (mates.m_num > 0)
		{
			for (Int i = 0; i < mates.m_num; ++i)
				rate += mates.m_fig[i]->m_cost * AICombatModel::killRate(f, mates.m_fig[i]);
			rate /= (mates.m_cost > 0.0f ? mates.m_cost : 1.0f);
		}
		else
		{
			rate = threat;
		}

		scoreB = common;
		if (rate > 0.0f)
		{
			scoreB += 55.0f + 40.0f * rate / (rate + 0.04f);
			if (f->m_supportLevel >= 2)
				scoreB += 8.0f;			// armed and mending the others: a priority among the threats
		}
		else if (f->m_supportLevel >= 2)
		{
			scoreB += 48.0f;		// healers and repairers keep the others going
			kind = 1;
		}
		else if (f->m_armed)
			scoreB += 25.0f;
		else if (f->m_supportLevel == 1)
			scoreB += 30.0f;		// workers
		else if (!f->m_structure)
			scoreB += 15.0f;
		else
			scoreB += 5.0f;

		// What outranges us hurts from where we cannot answer: when it is within reach, take it.
		if (f->m_armed && !f->m_structure && f->m_range >= 1.35f * mine->m_range && dist <= mine->m_range + 10.0f)
		{
			scoreB += 12.0f;
			kind = 2;
		}
	}
	out->m_original = scoreA;
	out->m_threat = scoreB;
	out->m_kind = kind;
}

Real splitPenalty( Real assigned, Real health )
{
	if (assigned <= 0.0f)
		return 0.0f;
	const Real ratio = assigned / (health * 1.05f + 1.0f);
	return ratio >= 1.0f ? 100.0f : 40.0f * ratio * ratio;
}

//-------------------------------------------------------------------------------------------------
// the split fire ledger
//-------------------------------------------------------------------------------------------------
void SplitLedger::clear()
{
	for (Int i = 0; i < SIZE; ++i)
	{
		m_entries[i].m_target = INVALID_ID;
		m_entries[i].m_damage = 0.0f;
		m_entries[i].m_expire = 0;
	}
}

Real SplitLedger::assigned( ObjectID target, UnsignedInt now ) const
{
	for (Int i = 0; i < SIZE; ++i)
	{
		const Entry &e = m_entries[i];
		if (e.m_target == target && e.m_expire > now)
			return e.m_damage;
	}
	return 0.0f;
}

void SplitLedger::assign( ObjectID target, Real damage, UnsignedInt now, UnsignedInt windowFrames )
{
	if (damage <= 0.0f)
		return;
	const UnsignedInt expire = now + windowFrames;

	Int free = -1;
	for (Int i = 0; i < SIZE; ++i)
	{
		Entry &e = m_entries[i];
		if (e.m_target == target && e.m_expire > now)
		{
			e.m_damage += damage;
			e.m_expire = expire;
			return;
		}
		// The first free (expired) slot, else the entry that expires first.
		if (e.m_expire <= now)
		{
			if (free < 0 || m_entries[free].m_expire > now)
				free = i;
		}
		else if (free < 0 || (m_entries[free].m_expire > now && e.m_expire < m_entries[free].m_expire))
		{
			free = i;
		}
	}
	Entry &e = m_entries[free];
	e.m_target = target;
	e.m_damage = damage;
	e.m_expire = expire;
}

//-------------------------------------------------------------------------------------------------
Bool playerStart( Player *player, Coord3D *out )
{
	if (player == nullptr)
		return FALSE;
	AsciiString name;
	name.format("Player_%d_Start", player->getMpStartIndex() + 1);
	Waypoint *way = TheTerrainLogic->getWaypointByName(name);
	if (way == nullptr)
		return FALSE;
	*out = *way->getLocation();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void rallyAhead( const Coord3D &base, Real baseRadius, const Coord3D &toward, Coord3D *out )
{
	Real dx = toward.x - base.x, dy = toward.y - base.y;
	Real len = sqrtf(dx * dx + dy * dy);
	if (len < 1.0f)
	{
		dx = 1.0f; dy = 0.0f; len = 1.0f;
	}
	const Real dist = baseRadius + 140.0f;
	out->x = base.x + dx / len * dist;
	out->y = base.y + dy / len * dist;
	out->z = TheTerrainLogic->getGroundHeight(out->x, out->y);
}

}	// namespace AITactics
