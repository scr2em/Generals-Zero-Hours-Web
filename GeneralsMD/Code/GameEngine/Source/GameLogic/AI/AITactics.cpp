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
	return m_ledger.assigned(target, TheGameLogic->getFrame());
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
	m_ledger.assign(target, damage, TheGameLogic->getFrame(), secondsToFrames(skill().m_splitWindowSeconds));
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
/// Kiting is switched on in the skill settings (Kiting = Yes) or for a test (variant "on-kite"), and not off for a test.
Bool AIStrategy::kitingOn() const
{
	return (skill().m_useKiting || m_ai->isFeatureForced(AIPlayer::AIF_KITE)) && !m_ai->isFeatureOff(AIPlayer::AIF_KITE);
}

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

/// The enemy model of the AI as the shared tactics see it.
namespace
{
	class EnemyContacts : public AITactics::ContactList
	{
	public:
		EnemyContacts(const AIEnemyModel &model) : m_model(model), m_now(TheGameLogic->getFrame()) { }
		virtual Int count() const override { return m_model.numContacts(); }
		virtual Bool get(Int index, AITactics::Contact *out) const override
		{
			const AIContact &c = m_model.contacts()[index];
			out->m_id = c.m_id;
			out->m_pos = c.m_pos;
			out->m_fig = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
			out->m_age = m_now - c.m_lastSeen;
			return TRUE;
		}
	private:
		const AIEnemyModel &m_model;
		UnsignedInt m_now;
	};
}

/// Is a step back worth it right now, and where to?  (The unit is attacking 'victim'.)  The rules are the shared ones (AITacticsCore.cpp).
Bool AIStrategy::planKite( Object *unit, Object *victim, const AITeamRecord *rec, Coord3D *to, UnsignedInt *until )
{
	(void)rec;
	const Coord3D *home = unit->getTeam() ? unit->getTeam()->getEstimateTeamPosition() : nullptr;
	EnemyContacts contacts(m_enemy);
	const AITactics::KiteResult result = AITactics::planKite(AITactics::paramsOf(skill()), unit, victim, home, contacts, to, until);
	if (result == AITactics::KITE_REJECT_FAST)
		++m_kiteRejectFast;
	else if (result == AITactics::KITE_REJECT_CORNER)
		++m_kiteRejectCorner;
	return result == AITactics::KITE_YES;
}

/// Looks at one unit of a field team.
void AIStrategy::unitTactics( Object *unit, AITeamRecord *rec )
{
	if (unit->isEffectivelyDead() || unit->isContained() || unit->isDisabled() || unit->getAI() == nullptr || isDetached(unit->getID()))
		return;
	const Bool kiting = kitingOn();
	const Bool spreading = m_spacing > 0.0f;
	if (rec->m_mode == AITEAM_RETREATING || findStep(unit->getID()) != nullptr || m_numSteps >= MAX_STEPS)
		return;

	AIUpdateInterface *ai = unit->getAI();
	Object *victim = ai->isAttacking() ? ai->getCurrentVictim() : nullptr;
	Coord3D to;
	UnsignedInt until;
	if (victim == nullptr)
	{
		// Standing around: do not stand in a clump while the enemy has area weapons.
		if (spreading && ai->isIdle() && planSpread(unit, nullptr, &to, &until))
		{
			ai->aiMoveToPosition(&to, CMD_FROM_AI);
			++m_spreadMoves;
		}
		return;
	}
	Bool kite = kiting && planKite(unit, victim, rec, &to, &until);
	if (!kite)
	{
		// Between two shots: step aside from the others.
		if (!spreading || !planSpread(unit, victim, &to, &until))
			return;
		++m_spreadSteps;
	}

	AIStepRecord &s = m_steps[m_numSteps++];
	s.m_unit = unit->getID();
	s.m_victim = victim->getID();
	s.m_until = until;
	s.m_phase = 0;
	ai->aiMoveToPosition(&to, CMD_FROM_AI);
	if (kite)
		++m_kiteStarts;
	if (kite && m_kiteStarts <= 12)
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
			else if (kitingOn() && planKite(unit, victim, nullptr, &to, &until))
			{
				s.m_phase = 0;
				s.m_until = until;
				ai->aiMoveToPosition(&to, CMD_FROM_AI);
				++m_kiteStarts;
			}
			else if (m_spacing > 0.0f && planSpread(unit, victim, &to, &until))
			{
				s.m_phase = 0;
				s.m_until = until;
				ai->aiMoveToPosition(&to, CMD_FROM_AI);
				++m_spreadSteps;
			}
		}

		if (finished)
		{
			// Back to the team's business, whatever it is now (it may have been pulled back meanwhile).
			const AITeamRecord *rec = unit->getTeam() ? findRecord(unit->getTeam()->getID(), FALSE) : nullptr;
			if (rec && (rec->m_mode == AITEAM_ATTACKING || rec->m_mode == AITEAM_DEFENDING) && (ai->isIdle() || ai->isAttacking()))
			{
				const Coord3D target = rec->m_target;
				ai->aiAttackMoveToPosition(&target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
			}
			dropStep(&s);
			continue;
		}
		++i;
	}
}

/// Per frame: the units on a step, and a few more units of the field teams.
void AIStrategy::updateTactics()
{
	const Bool kiting = kitingOn();
	if (!kiting && m_spacing <= 0.0f && m_numSteps == 0)
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

//-------------------------------------------------------------------------------------------------
// spreading out
//
// Area weapons (artillery shells, toxin, nukes: any enemy weapon with a blast of SplashRadiusThreshold or more)
// punish a clump.  While the enemy model shows such weapons, units keep their distance from each other:
// an idle unit that stands too close to others moves apart, and a unit that waits for its weapon to reload
// steps aside (and turns back to its target, like a kite).  Only the army's own units that stand still are
// moved; units on the march keep to the group's path.
//-------------------------------------------------------------------------------------------------
void AIStrategy::refreshSplashThreat()
{
	const AISkillSettings &sk = skill();
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!sk.m_useSpread || m_ai->isFeatureOff(AIPlayer::AIF_SPREAD))
	{
		m_spacing = 0.0f;
		return;
	}

	// The biggest blast among the enemy kinds that matter (a share of the armed force, or huge).
	const Real armed = m_enemy.armedValue();
	Real blast = 0.0f;
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		const AIComposition &c = m_enemy.composition()[e];
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || f->m_maxSplash < sk.m_splashRadiusThreshold)
			continue;
		if ((c.m_value >= 0.04f * armed || f->m_maxSplash >= 2.5f * sk.m_splashRadiusThreshold) && f->m_maxSplash > blast)
			blast = f->m_maxSplash;
	}

	if (blast > 0.0f)
	{
		const Real spacing = AITactics::spacingForBlast(AITactics::paramsOf(sk), blast);
		if (m_spacing <= 0.0f)
			AI_TRACE("enemy area weapons seen (blast %.0f): keeping %.0f apart", blast, spacing);
		m_spacing = spacing;
		m_spacingUntil = now + 45 * LOGICFRAMES_PER_SECOND;
	}
	else if (m_spacing > 0.0f && now >= m_spacingUntil)
	{
		AI_TRACE("no area weapons seen for a while: no need to spread out");
		m_spacing = 0.0f;
	}
}

/**
 * A spot to move to because 'unit' stands too close to its friends, or false.  With a victim, the unit is between
 * two shots: the spot must stay in range of the victim.  (The rules are the shared ones, AITacticsCore.cpp.)
 */
Bool AIStrategy::planSpread( Object *unit, Object *victim, Coord3D *to, UnsignedInt *until )
{
	const Coord3D *home = unit->getTeam() ? unit->getTeam()->getEstimateTeamPosition() : nullptr;
	return AITactics::planSpread(AITactics::paramsOf(skill()), m_spacing, unit, victim, home, to, until);
}

//-------------------------------------------------------------------------------------------------
// defend the workers
//
// The gatherers and workers of the player are the protected objects of a protect relation (AIProtect.cpp); the
// armed units of the field teams that are at home (not on a wave, not retreating) are its protectors.  When an
// enemy damages a gatherer or a worker, the nearest suitable protectors go for the attacker and return to where they
// stood afterwards.  The relation is refreshed every two seconds (units come and go); the answers are given by the
// protect module, once per frame.
//-------------------------------------------------------------------------------------------------
Bool AIStrategy::protectOn() const
{
	return (skill().m_useProtect || m_ai->isFeatureForced(AIPlayer::AIF_PROTECT)) && !m_ai->isFeatureOff(AIPlayer::AIF_PROTECT);
}

void AIStrategy::updateProtection()
{
	m_protect.update();
	if (!protectOn())
	{
		if (m_protectActive)
		{
			m_protect.release(1);
			m_protectActive = FALSE;
		}
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextProtect)
		return;
	m_nextProtect = now + 2 * LOGICFRAMES_PER_SECOND;
	const AISkillSettings &sk = skill();

	// The protected: our gatherers and workers.
	ObjectID protectedSet[AIProtect::MAX_PROTECTED];
	Int numProtected = 0;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_STRUCTURE) || numProtected >= AIProtect::MAX_PROTECTED)
					continue;
				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER))
					protectedSet[numProtected++] = obj->getID();
			}
		}
	}

	// The protectors: armed mobile units of the teams that are at home.
	ObjectID protectors[AIProtect::MAX_PROTECTORS];
	Int numProtectors = 0;
	for (Int i = 0; i < m_numTeams && numProtectors < AIProtect::MAX_PROTECTORS; ++i)
	{
		const AITeamRecord &rec = m_teams[i];
		if (rec.m_mode == AITEAM_ATTACKING || rec.m_mode == AITEAM_RETREATING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done() && numProtectors < AIProtect::MAX_PROTECTORS; it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr || isDetached(obj->getID()))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || f->m_structure || f->m_airborne || f->m_speed <= 0.0f)
				continue;
			BodyModuleInterface *body = obj->getBodyModule();
			if (body && body->getMaxHealth() > 0.0f && body->getHealth() < 0.4f * body->getMaxHealth())
				continue;
			protectors[numProtectors++] = obj->getID();
		}
	}

	if (numProtected == 0 || numProtectors == 0)
	{
		if (m_protectActive)
		{
			m_protect.release(1);
			m_protectActive = FALSE;
		}
		return;
	}

	AIProtectParams params;
	params.m_leashRadius = sk.m_protectLeash;
	params.m_responseRadius = sk.m_protectResponseRadius;
	params.m_calmSeconds = sk.m_protectCalmSeconds;
	params.m_maxSeconds = sk.m_protectMaxSeconds;
	params.m_maxResponders = sk.m_protectResponders;
	m_protect.assign(1, protectors, numProtectors, protectedSet, numProtected, params, nullptr);
	m_protectActive = TRUE;
}
