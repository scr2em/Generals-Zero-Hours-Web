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

// AITactics.cpp
// Unit level tactics of the Expert computer player, on top of the strategic layer (AIStrategy.cpp):
// split fire, kiting, spreading out against area weapons.  Like the rest of the Expert AI this only reads
// synchronised game state, never uses the wall clock, and keeps its memory in plain arrays.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
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


//-------------------------------------------------------------------------------------------------
// split fire
//
// A unit that picks a target tells the ledger how much damage it is about to deal to it.  Units that
// pick a target a moment later see what is already on its way and take the next target once the
// first ones are enough to kill it.  An entry expires after the split window of the skill settings
// (the time the shots take to land), so a target that survives becomes a candidate again.
//-------------------------------------------------------------------------------------------------
Real AIStrategy::assignedDamage( ObjectID target ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = 0; i < LEDGER_SIZE; ++i)
	{
		const AILedgerEntry &e = m_ledger[i];
		if (e.m_target == target && e.m_expire > now)
			return e.m_damage;
	}
	return 0.0f;
}

void AIStrategy::assignDamage( ObjectID target, Real damage, Int flags )
{
	++m_splitPicks;
	if (flags & AIPlayer::PICK_SPLIT)
		++m_splitSwitches;
	if (flags & AIPlayer::PICK_THREAT)
		++m_threatSwitches;
	if (flags & AIPlayer::PICK_SUPPORT)
		++m_supportPicks;
	if (flags & AIPlayer::PICK_LONGRANGE)
		++m_longRangePicks;
	if (damage <= 0.0f)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt expire = now + secondsToFrames(skill().m_splitWindowSeconds);

	Int free = -1;
	for (Int i = 0; i < LEDGER_SIZE; ++i)
	{
		AILedgerEntry &e = m_ledger[i];
		if (e.m_target == target && e.m_expire > now)
		{
			e.m_damage += damage;
			e.m_expire = expire;
			return;
		}
		// The first free (expired) slot, else the entry that expires first.
		if (e.m_expire <= now)
		{
			if (free < 0 || m_ledger[free].m_expire > now)
				free = i;
		}
		else if (free < 0 || (m_ledger[free].m_expire > now && e.m_expire < m_ledger[free].m_expire))
		{
			free = i;
		}
	}
	AILedgerEntry &e = m_ledger[free];
	e.m_target = target;
	e.m_damage = damage;
	e.m_expire = expire;
}

//-------------------------------------------------------------------------------------------------
// kiting
//
// A unit that is not slower than its enemy and out-ranges it (or is clearly faster and has a similar
// range) does not stand still while its weapon reloads: it backs away to the edge of its range, so the
// enemy spends the reload time walking instead of shooting, and turns to fire again when the weapon is
// ready.  It does not kite when
//   * a faster enemy is about (it would be run down), or
//   * there is no room: the spot behind it is off the map or blocked, or other enemies cover it, or
//   * it would leave its team (KiteGroupRadius).
// Units are looked at round robin a few per frame; the units that are on a step are looked at every
// other frame.
//-------------------------------------------------------------------------------------------------
AIStepRecord *AIStrategy::findStep( ObjectID unit )
{
	for (Int i = 0; i < m_numSteps; ++i)
	{
		if (m_steps[i].m_unit == unit)
			return &m_steps[i];
	}
	return nullptr;
}

void AIStrategy::dropStep( AIStepRecord *rec )
{
	*rec = m_steps[--m_numSteps];
}

/// Does the enemy unit show on our side?  (A unit we cannot see is no target to go back to.)
Bool AIStrategy::enemyCanSee( const Object *victim ) const
{
	if (victim->isEffectivelyDead() || victim->isOffMap())
		return FALSE;
	if (victim->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
		return FALSE;
	if (victim->testStatus(OBJECT_STATUS_STEALTHED) && !victim->testStatus(OBJECT_STATUS_DETECTED))
		return FALSE;
	return TRUE;
}

/// Is a step back worth it right now, and where to?  (The unit is attacking 'victim'.)
Bool AIStrategy::planKite( Object *unit, Object *victim, const AITeamRecord *rec, Coord3D *to, UnsignedInt *until )
{
	const AISkillSettings &sk = skill();
	const UnsignedInt now = TheGameLogic->getFrame();
	AIUpdateInterface *ai = unit->getAI();
	if (ai == nullptr)
		return FALSE;

	const AICombatFigures *mf = AICombatModel::figures(unit->getTemplate());
	const AICombatFigures *vf = AICombatModel::figures(victim->getTemplate());
	if (mf == nullptr || vf == nullptr)
		return FALSE;
	if (mf->m_structure || mf->m_airborne || mf->m_speed <= 0.0f || mf->m_range < 60.0f)
		return FALSE;
	// Only mobile fighters that can hurt us chase us; a building or a harmless unit gives no reason to move.
	if (vf->m_structure || vf->m_airborne || vf->m_speed <= 0.0f || !vf->m_armed || AICombatModel::damagePerSecond(vf, mf) <= 0.0f)
		return FALSE;

	// Can we keep away from it?
	const Real ratio = mf->m_speed / vf->m_speed;
	const Bool outranges = ratio >= 1.0f && mf->m_range >= sk.m_kiteRangeFactor * vf->m_range;
	const Bool outruns = ratio >= sk.m_kiteSpeedFactor && mf->m_range >= 0.95f * vf->m_range;
	if (!outranges && !outruns)
		return FALSE;

	// The weapon must be waiting for a while.
	Weapon *weapon = unit->getCurrentWeapon();
	if (weapon == nullptr)
		return FALSE;
	const WeaponStatus status = weapon->getStatus();
	if (status != BETWEEN_FIRING_SHOTS && status != RELOADING_CLIP)
		return FALSE;
	const UnsignedInt next = weapon->getPossibleNextShotFrame();
	if (next <= now + secondsToFrames(sk.m_kiteMinReloadSeconds))
		return FALSE;
	const Real gapSeconds = (Real)(next - now) / LOGICFRAMES_PER_SECOND;

	// Geometry: back to the edge of our range, directly away from the enemy.
	const Coord3D &up = *unit->getPosition();
	const Coord3D &vp = *victim->getPosition();
	const Real dx = up.x - vp.x, dy = up.y - vp.y;
	const Real dist = sqrtf(dx * dx + dy * dy);
	if (dist < 1.0f)
		return FALSE;
	const Real myRange = weapon->getAttackRange(unit);
	const Real want = myRange - 10.0f;
	if (dist >= want - 12.0f)
		return FALSE;		// already at the edge of the range
	// The enemy could not reach us during the wait anyway: stay and keep the aim.
	if (dist > vf->m_range + vf->m_speed * gapSeconds * 0.8f + 20.0f)
		return FALSE;

	Real step = want - dist;
	const Real maxStep = mf->m_speed * gapSeconds * 0.45f;		// there and back within the wait
	if (step > maxStep)
		step = maxStep;
	if (step < 12.0f)
		return FALSE;
	to->x = up.x + dx / dist * step;
	to->y = up.y + dy / dist * step;
	to->z = TheTerrainLogic->getGroundHeight(to->x, to->y);

	// Faster enemies about: kiting would only get the unit run down.
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 3 * LOGICFRAMES_PER_SECOND || dist2D(c.m_pos, up) > myRange + 150.0f)
			continue;
		const AICombatFigures *cf = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (cf && cf->m_armed && !cf->m_airborne && cf->m_speed > mf->m_speed * 1.02f && AICombatModel::damagePerSecond(cf, mf) > 0.0f)
		{
			++m_kiteRejectFast;
			return FALSE;
		}
	}

	// Room behind us: on the map, passable, not under the guns of other enemies, and not away from our team.
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	if (to->x < extent.lo.x + 40.0f || to->x > extent.hi.x - 40.0f || to->y < extent.lo.y + 40.0f || to->y > extent.hi.y - 40.0f ||
			!ai->isValidLocomotorPosition(to))
	{
		++m_kiteRejectCorner;
		return FALSE;
	}
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_id == victim->getID() || c.m_role > AIROLE_DEFENCE || (c.m_role < AIROLE_DEFENCE && now - c.m_lastSeen > 3 * LOGICFRAMES_PER_SECOND))
			continue;
		const AICombatFigures *cf = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (cf && cf->m_armed && cf->m_canHitGround && dist2D(c.m_pos, *to) < cf->m_range + 20.0f && dist2D(c.m_pos, up) >= cf->m_range + 20.0f)
		{
			++m_kiteRejectCorner;
			return FALSE;
		}
	}
	const Coord3D *home = unit->getTeam() ? unit->getTeam()->getEstimateTeamPosition() : nullptr;
	if (home && dist2D(*home, *to) > sk.m_kiteGroupRadius)
		return FALSE;

	const UnsignedInt stepFrames = (UnsignedInt)(step / mf->m_speed * LOGICFRAMES_PER_SECOND) + 15;
	*until = now + (stepFrames < next - now ? stepFrames : next - now);
	(void)rec;
	return TRUE;
}

/// Looks at one unit of a field team.
void AIStrategy::unitTactics( Object *unit, AITeamRecord *rec )
{
	if (unit->isEffectivelyDead() || unit->isContained() || unit->isDisabled() || unit->getAI() == nullptr)
		return;
	if (!skill().m_useKiting || m_ai->isFeatureOff(AIPlayer::AIF_KITE))
		return;
	if (rec->m_mode == AITEAM_RETREATING || findStep(unit->getID()) != nullptr || m_numSteps >= MAX_STEPS)
		return;

	AIUpdateInterface *ai = unit->getAI();
	if (!ai->isAttacking())
		return;
	Object *victim = ai->getCurrentVictim();
	if (victim == nullptr)
		return;
	Coord3D to;
	UnsignedInt until;
	if (!planKite(unit, victim, rec, &to, &until))
		return;

	AIStepRecord &s = m_steps[m_numSteps++];
	s.m_unit = unit->getID();
	s.m_victim = victim->getID();
	s.m_until = until;
	s.m_phase = 0;
	s.m_hasResume = (rec->m_mode == AITEAM_ATTACKING || rec->m_mode == AITEAM_DEFENDING);
	s.m_resume = rec->m_target;
	ai->aiMoveToPosition(&to, CMD_FROM_AI);
	++m_kiteStarts;
	if (m_kiteStarts <= 12)
		AI_TRACE("unit %u (%s) steps back from %u (%s) to (%.0f,%.0f)", unit->getID(), unit->getTemplate()->getName().str(), victim->getID(),
			victim->getTemplate()->getName().str(), to.x, to.y);
}

/// The units that are on a step: turn back to the fight when the weapon is ready, and carry on afterwards.
void AIStrategy::updateSteps()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = 0; i < m_numSteps; )
	{
		AIStepRecord &s = m_steps[i];
		Object *unit = TheGameLogic->findObjectByID(s.m_unit);
		if (unit == nullptr || unit->isEffectivelyDead() || unit->getAI() == nullptr)
		{
			dropStep(&s);
			continue;
		}
		AIUpdateInterface *ai = unit->getAI();
		Object *victim = TheGameLogic->findObjectByID(s.m_victim);
		const Bool victimOk = victim != nullptr && enemyCanSee(victim);
		Bool finished = FALSE;

		if (s.m_phase == 0)
		{
			Weapon *weapon = unit->getCurrentWeapon();
			const Bool ready = weapon == nullptr || weapon->getPossibleNextShotFrame() <= now + 8;
			if (now >= s.m_until || ready || ai->isIdle())
			{
				if (victimOk)
				{
					ai->aiAttackObject(victim, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					s.m_phase = 1;
					s.m_until = now + 12 * LOGICFRAMES_PER_SECOND;
					++m_kiteResumes;
				}
				else
				{
					finished = TRUE;
				}
			}
		}
		else
		{
			// Attacking again.  Fired?  Then another step if it is worth it; the target gone: back to the team's business.
			Coord3D to;
			UnsignedInt until;
			const UnsignedInt phaseStart = s.m_until - 12 * LOGICFRAMES_PER_SECOND;
			if (!victimOk || (now >= phaseStart + 10 && ai->getCurrentVictim() != victim))
			{
				finished = TRUE;
			}
			else if (now >= s.m_until)
			{
				dropStep(&s);
				continue;
			}
			else if (planKite(unit, victim, nullptr, &to, &until))
			{
				s.m_phase = 0;
				s.m_until = until;
				ai->aiMoveToPosition(&to, CMD_FROM_AI);
				++m_kiteStarts;
			}
		}

		if (finished)
		{
			if (s.m_hasResume && (ai->isIdle() || ai->isAttacking()))
				ai->aiAttackMoveToPosition(&s.m_resume, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
			dropStep(&s);
			continue;
		}
		++i;
	}
}

/// Per frame: the units on a step, and a few more units of the field teams.
void AIStrategy::updateTactics()
{
	if (!skill().m_useKiting || m_ai->isFeatureOff(AIPlayer::AIF_KITE))
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if ((now & 1) == 0 && m_numSteps > 0)
		updateSteps();

	Int budget = 4;
	while (budget-- > 0 && m_numTeams > 0)
	{
		if (m_tacticTeam >= m_numTeams)
		{
			m_tacticTeam = 0;
			m_tacticUnit = 0;
		}
		AITeamRecord *rec = &m_teams[m_tacticTeam];
		Team *team = TheTeamFactory->findTeamByID(rec->m_team);
		Object *unit = nullptr;
		if (team)
		{
			Int index = 0;
			for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance(), ++index)
			{
				if (index == m_tacticUnit)
				{
					unit = it.cur();
					break;
				}
			}
		}
		if (unit == nullptr)
		{
			++m_tacticTeam;
			m_tacticUnit = 0;
			continue;
		}
		++m_tacticUnit;
		unitTactics(unit, rec);
	}
}
