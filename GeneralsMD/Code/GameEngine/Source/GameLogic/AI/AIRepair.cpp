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

// AIRepair.cpp
// Repair and healing for the Expert computer player.
//
//  * A damaged vehicle goes to one of our repair pads (an object with KindOf REPAIR_PAD: a war factory with a
//    RepairDockUpdate, a repair bay ...), a damaged infantryman to a heal pad (KindOf HEAL_PAD: a hospital or
//    ambulance with a HealContain), when none of the enemy is near and the trip is short enough.  The unit is
//    "detached" from its team on the way and while it is mended (AIStrategy::isDetached) and rejoins afterwards.
//    Whether a unit can use a pad is asked of the action manager, so the rules of the game data apply.
//  * Dozers and workers: structures that are too far from every dozer for its own "bored" repair routine
//    (DozerAIUpdate repairs what is within its bored range) are repaired by an idle dozer in a lull.
//
// Everything uses the templates and what is in sight; no template names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
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
#include "GameLogic/Module/DozerAIUpdate.h"
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

enum { PATIENT_REPAIR = 0, PATIENT_HEAL = 1, PATIENT_BUILDING = 2 };
enum { PHASE_GOING = 0, PHASE_RETURNING = 1 };

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::repairOn() const
{
	return (skill().m_useRepair || m_ai->isFeatureForced(AIPlayer::AIF_REPAIR)) && !m_ai->isFeatureOff(AIPlayer::AIF_REPAIR);
}

Bool AIStrategy::isPatient( ObjectID id ) const
{
	for (Int i = 0; i < m_numPatients; ++i)
	{
		if (m_patients[i].m_unit == id)
			return TRUE;
	}
	return FALSE;
}

/// The repair pads and heal pads we own (a list that is renewed every few seconds).
void AIStrategy::refreshRepairSites()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextSiteScan)
		return;
	m_nextSiteScan = now + 4 * LOGICFRAMES_PER_SECOND;
	m_numSites = 0;
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
				if (obj == nullptr || obj->isEffectivelyDead() || m_numSites >= MAX_SITES)
					continue;
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD))
					continue;
				if (obj->isKindOf(KINDOF_REPAIR_PAD) || obj->isKindOf(KINDOF_HEAL_PAD))
					m_sites[m_numSites++] = obj->getID();
			}
		}
	}
}

/// The nearest pad that suits the unit (asked of the action manager), within the longest trip the settings allow.
Object *AIStrategy::findRepairSite( Object *unit, Int kind ) const
{
	const AICombatFigures *f = AICombatModel::figures(unit->getTemplate());
	const Real maxDist = skill().m_repairTripSeconds * (f && f->m_speed > 0.0f ? f->m_speed : 30.0f);
	Object *best = nullptr;
	Real bestDist = 0.0f;
	for (Int i = 0; i < m_numSites; ++i)
	{
		Object *site = TheGameLogic->findObjectByID(m_sites[i]);
		if (site == nullptr || site->isEffectivelyDead() || site->isDisabled())
			continue;
		const Bool ok = kind == PATIENT_REPAIR ? TheActionManager->canGetRepairedAt(unit, site, CMD_FROM_AI)
																					 : TheActionManager->canGetHealedAt(unit, site, CMD_FROM_AI);
		if (!ok)
			continue;
		// Not too many at one pad.
		Int visitors = 0;
		for (Int p = 0; p < m_numPatients; ++p)
			if (m_patients[p].m_site == site->getID())
				++visitors;
		if (visitors >= 2)
			continue;
		const Real d = dist2D(*unit->getPosition(), *site->getPosition());
		if (d > maxDist)
			continue;
		if (best == nullptr || d < bestDist)
		{
			best = site;
			bestDist = d;
		}
	}
	return best;
}

/// Back to the team: its position, or on with the wave.
void AIStrategy::sendPatientBack( Object *unit )
{
	AIUpdateInterface *ai = unit->getAI();
	if (ai == nullptr || unit->isEffectivelyDead())
		return;
	const AITeamRecord *rec = unit->getTeam() ? findRecord(unit->getTeam()->getID(), FALSE) : nullptr;
	if (rec && (rec->m_mode == AITEAM_ATTACKING || rec->m_mode == AITEAM_DEFENDING))
	{
		const Coord3D target = rec->m_target;
		ai->aiAttackMoveToPosition(&target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		return;
	}
	Coord3D rally;
	if (rallyPoint(&rally))
		ai->aiMoveToPosition(&rally, CMD_FROM_AI);
}

void AIStrategy::dropPatient( Int index )
{
	m_patients[index] = m_patients[--m_numPatients];
}

/// The units on their way to a pad (or back): finished, lost, stuck, or out of time.
void AIStrategy::updatePatients()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = 0; i < m_numPatients; )
	{
		AIPatient &p = m_patients[i];
		Object *unit = TheGameLogic->findObjectByID(p.m_unit);
		if (unit == nullptr || unit->isEffectivelyDead() || unit->getAI() == nullptr)
		{
			dropPatient(i);
			continue;
		}
		AIUpdateInterface *ai = unit->getAI();
		Object *site = TheGameLogic->findObjectByID(p.m_site);
		const Bool siteGone = site == nullptr || site->isEffectivelyDead();
		const Real health = healthShare(unit);

		if (p.m_phase == PHASE_RETURNING)
		{
			if (now - p.m_phaseFrame > 6 * LOGICFRAMES_PER_SECOND || ai->isIdle())
			{
				dropPatient(i);
				continue;
			}
			++i;
			continue;
		}

		Bool done = FALSE;
		const char *why = nullptr;
		if (p.m_kind == PATIENT_BUILDING)
		{
			// A dozer on a structure: until the structure is whole, or the lull is over.
			if (siteGone || healthShare(site) >= 0.98f)
			{
				done = TRUE;
				why = "repaired";
				++m_repairsDone;
			}
			else if (m_threatSince != 0 || m_player->getAttackedFrame() + 8 * LOGICFRAMES_PER_SECOND > now)
			{
				done = TRUE;
				why = "the base is under attack";
				ai->aiIdle(CMD_FROM_AI);
			}
			else if (now - p.m_start > 60 * LOGICFRAMES_PER_SECOND)
			{
				done = TRUE;
				why = "taking too long";
			}
			if (done)
			{
				AI_TRACE("dozer %u: %s (%s)", unit->getID(), why, p.m_kind == PATIENT_BUILDING ? "structure" : "");
				dropPatient(i);
				continue;
			}
			++i;
			continue;
		}

		if (health >= 0.97f && !unit->isContained())
		{
			done = TRUE;
			why = "mended";
			++m_repairsDone;
		}
		else if (siteGone)
		{
			done = TRUE;
			why = "the pad is gone";
		}
		else if (now - p.m_start > secondsToFrames(skill().m_repairTripSeconds * 2.0f + 40.0f))
		{
			done = TRUE;
			why = "taking too long";
		}
		else if (!unit->isContained() && ai->isIdle() && now - p.m_start > 5 * LOGICFRAMES_PER_SECOND && now - p.m_lastOrder > 4 * LOGICFRAMES_PER_SECOND)
		{
			// Not on its way: the order was not taken, or the pad is busy.  Once more, then give up.
			if (p.m_retries >= 1)
			{
				done = TRUE;
				why = "the pad cannot be reached";
			}
			else
			{
				++p.m_retries;
				p.m_lastOrder = now;
				if (p.m_kind == PATIENT_REPAIR)
					ai->aiGetRepaired(site, CMD_FROM_AI);
				else
					ai->aiGetHealed(site, CMD_FROM_AI);
			}
		}
		if (done)
		{
			AI_TRACE("%s %u (%s) %s: health %.0f%%", p.m_kind == PATIENT_REPAIR ? "repair" : "heal", unit->getID(), unit->getTemplate()->getName().str(), why, health * 100.0f);
			p.m_phase = PHASE_RETURNING;
			p.m_phaseFrame = now;
			sendPatientBack(unit);
		}
		++i;
	}
}

/// Look for damaged units that should go to a pad, and for structures that no dozer would reach on its own.
void AIStrategy::tryStartRepairs()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	refreshRepairSites();

	// Units.
	if (m_numSites > 0 && m_numPatients < MAX_PATIENTS)
	{
		const Real below = sk.m_repairBelow;
		Object *pick = nullptr;
		Real pickHealth = 2.0f;
		Int pickKind = PATIENT_REPAIR;
		for (Int i = 0; i < m_numTeams; ++i)
		{
			const AITeamRecord &rec = m_teams[i];
			Team *team = TheTeamFactory->findTeamByID(rec.m_team);
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
			{
				Object *obj = it.cur();
				if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr || isDetached(obj->getID()))
					continue;
				const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
				if (f == nullptr || f->m_structure || f->m_airborne || f->m_speed <= 0.0f)
					continue;
				const Bool vehicle = obj->isKindOf(KINDOF_VEHICLE);
				const Bool infantry = obj->isKindOf(KINDOF_INFANTRY);
				if (!vehicle && !infantry)
					continue;
				const Real health = healthShare(obj);
				// A unit in a wave that is out goes only when it is nearly dead; at home it goes when it is hurt.
				const Real limit = (rec.m_mode == AITEAM_ATTACKING) ? 0.6f * below : below;
				if (health >= limit || health >= pickHealth)
					continue;
				// Not in the middle of a fight.
				if (armyValueNear(obj->getPosition(), 260.0f, TRUE) > 0.0f)
					continue;
				pick = obj;
				pickHealth = health;
				pickKind = vehicle ? PATIENT_REPAIR : PATIENT_HEAL;
			}
		}
		if (pick)
		{
			Object *site = findRepairSite(pick, pickKind);
			if (site)
			{
				AIPatient &p = m_patients[m_numPatients++];
				memset(&p, 0, sizeof(p));
				p.m_unit = pick->getID();
				p.m_site = site->getID();
				p.m_kind = pickKind;
				p.m_start = now;
				p.m_lastOrder = now;
				p.m_phase = PHASE_GOING;
				++m_repairTrips;
				AI_TRACE("%s %u (%s, health %.0f%%) goes to %s %u, %.0f away", pickKind == PATIENT_REPAIR ? "REPAIR" : "HEAL", pick->getID(), pick->getTemplate()->getName().str(),
					pickHealth * 100.0f, site->getTemplate()->getName().str(), site->getID(), dist2D(*pick->getPosition(), *site->getPosition()));
				if (pickKind == PATIENT_REPAIR)
					pick->getAI()->aiGetRepaired(site, CMD_FROM_AI);
				else
					pick->getAI()->aiGetHealed(site, CMD_FROM_AI);
			}
		}
	}

	// Structures in a lull: idle dozers that have nothing pending go to damaged structures out of the reach of their own routine.
	if (m_numPatients < MAX_PATIENTS && m_threatSince == 0 && m_player->getAttackedFrame() + 20 * LOGICFRAMES_PER_SECOND < now)
	{
		Object *dozer = nullptr;
		Object *building = nullptr;
		Real buildingHealth = 2.0f;
		for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end() && building == nullptr; ++it)
		{
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
			{
				Team *team = iter.cur();
				if (team == nullptr)
					continue;
				for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
				{
					Object *obj = oit.cur();
					if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE))
						continue;
					if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD))
						continue;
					Bool taken = FALSE;
					for (Int p = 0; p < m_numPatients && !taken; ++p)
						taken = m_patients[p].m_kind == PATIENT_BUILDING && m_patients[p].m_site == obj->getID();
					if (taken)
						continue;
					const Real h = healthShare(obj);
					if (h >= sk.m_repairDozerBelow || h >= buildingHealth)
						continue;
					building = obj;
					buildingHealth = h;
				}
			}
		}
		if (building)
		{
			// The nearest dozer that is idle and not busy with a job of the base builder.
			Real bestD = 0.0f;
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
						if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_DOZER) || obj->getAI() == nullptr || isDetached(obj->getID()))
							continue;
						DozerAIInterface *dozerAI = obj->getAI()->getDozerAIInterface();
						if (dozerAI == nullptr || !obj->getAI()->isIdle() || dozerAI->isAnyTaskPending() || obj->getID() == m_ai->m_repairDozer)
							continue;
						if (!TheActionManager->canRepairObject(obj, building, CMD_FROM_AI))
							continue;
						const Real d = dist2D(*obj->getPosition(), *building->getPosition());
						if (dozer == nullptr || d < bestD)
						{
							dozer = obj;
							bestD = d;
						}
					}
				}
			}
			// Only what the dozer's own routine would not reach.
			if (dozer)
			{
				const DozerAIInterface *dozerAI = dozer->getAI()->getDozerAIInterface();
				if (bestD > dozerAI->getBoredRange() + 20.0f)
				{
					AIPatient &p = m_patients[m_numPatients++];
					memset(&p, 0, sizeof(p));
					p.m_unit = dozer->getID();
					p.m_site = building->getID();
					p.m_kind = PATIENT_BUILDING;
					p.m_start = now;
					p.m_lastOrder = now;
					p.m_phase = PHASE_GOING;
					++m_dozerRepairs;
					AI_TRACE("DOZER %u repairs %s %u (health %.0f%%), %.0f away", dozer->getID(), building->getTemplate()->getName().str(), building->getID(), buildingHealth * 100.0f, bestD);
					dozer->getAI()->aiRepair(building, CMD_FROM_AI);
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::updateRepair()
{
	if (!repairOn())
	{
		if (m_numPatients > 0)
			m_numPatients = 0;
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextRepair)
		return;
	m_nextRepair = now + LOGICFRAMES_PER_SECOND;
	updatePatients();
	tryStartRepairs();
}
