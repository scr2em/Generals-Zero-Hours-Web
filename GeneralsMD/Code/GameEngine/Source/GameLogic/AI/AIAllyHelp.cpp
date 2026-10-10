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

// AIAllyHelp.cpp
// Allied base support: the Expert computer player sends help to an ally whose base is under attack.
//
// The base zone of an ally (a human or another computer player) is the bounds of its structures around its start position plus
// a margin, as for our own base.  A threat there is an armed enemy force that we have seen in the zone (with the shared vision
// of a team game, what the ally sees too), or an enemy that has just damaged one of the ally's objects inside it: the body
// module reports a hit on an object to the allies of its owner (the "our ally is under attack" message of the game).
//
// What is sent: the force the threat needs (AllyHelpForce times its value) less the armed units of the ally (and of the other
// allies) that are in the zone, the ally's defences that cover the place, and our units that are there already.  It comes from
// the teams at home and from the teams of a wave that are near the ally's base, the nearest first; teams farther away than
// AllyHelpMaxDistance are not sent.  Our own base has priority: no help while our base defence has an alarm (help that is out
// comes back), and against enemies that have been seen near our base, at least that much stays at home, and one team.  A force
// that would still be too weak for the fight (AllyHelpMinAdvantage) is not sent.  While help is out no wave is launched.
// The teams go back to their role when the ally's base has been clear for AllyHelpClearSeconds, or when the alarm has lasted
// AllyHelpMaxSeconds without anything of the ally being hit for a while (the force is standing about; it is ignored then
// for 90 s unless it hits something).
//
// Only what the player has seen (the enemy model, partition queries with the shroud test of the player) and the damage reports
// are used; the players are visited by index and the objects in the order of the partition manager, so it is deterministic.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

/// Structures this far from the start position of a player are not part of its base (expansions, captured buildings).
static const Real ALLY_BASE_REACH = 1200.0f;
/// Enemies seen this far beyond our base zone are a known threat to our own base.
static const Real HOME_WATCH = 600.0f;

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::allyHelpOn() const
{
	return skill().m_useAllyHelp && !m_ai->isFeatureOff(AIPlayer::AIF_ALLYHELP);
}

/// Is the team one of those sent to an ally (and still on that task)?
Bool AIStrategy::isAllyHelper( TeamID id ) const
{
	for (Int i = 0; i < m_ahNumTeams; ++i)
	{
		if (m_ahTeams[i] == id)
			return TRUE;
	}
	return FALSE;
}

/// The bases of the allies: the structures of each allied player near its start position, every ten seconds.
void AIStrategy::refreshAllyBases()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextAllyBases)
		return;
	m_nextAllyBases = now + 10 * LOGICFRAMES_PER_SECOND;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		m_allyRadius[i] = 0.0f;
	const Int count = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < count && i < MAX_PLAYER_COUNT; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (p == nullptr || p == m_player || p->getDefaultTeam() == nullptr || !p->isPlayerActive())
			continue;
		if (m_player->getRelationship(p->getDefaultTeam()) != ALLIES)
			continue;
		Coord3D start;
		const Bool haveStart = AITactics::playerStart(p, &start);
		// Two passes over the structures: the center, then the radius.
		Coord3D sum;
		sum.zero();
		Int n = 0;
		for (Int pass = 0; pass < 2; ++pass)
		{
			Real radius = 0.0f;
			Coord3D center = sum;
			if (pass == 1)
			{
				if (n == 0)
					break;
				center.x /= n;
				center.y /= n;
			}
			for (Player::PlayerTeamList::const_iterator it = p->getPlayerTeams()->begin(); it != p->getPlayerTeams()->end(); ++it)
			{
				for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
				{
					Team *team = iter.cur();
					if (team == nullptr)
						continue;
					for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
					{
						Object *obj = oit.cur();
						if (obj == nullptr || !obj->isKindOf(KINDOF_STRUCTURE) || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_MINE))
							continue;
						if (haveStart && dist2D(*obj->getPosition(), start) > ALLY_BASE_REACH)
							continue;
						if (pass == 0)
						{
							sum.x += obj->getPosition()->x;
							sum.y += obj->getPosition()->y;
							++n;
						}
						else
						{
							const Real d = dist2D(*obj->getPosition(), center);
							if (d > radius)
								radius = d;
						}
					}
				}
			}
			if (pass == 1)
			{
				center.z = 0.0f;
				m_allyBase[i] = center;
				m_allyRadius[i] = radius < 150.0f ? 150.0f : (radius > 1000.0f ? 1000.0f : radius);
			}
		}
	}
}

/// One of our objects, or of an ally's, took damage from an enemy (reported by the body module, AIProtectNotifyDamage).
void AIStrategy::onObjectDamaged( Object *victim, ObjectID attacker, Real amount )
{
	if (victim->getControllingPlayer() != m_player)
	{
		onAllyObjectDamaged(victim, attacker, amount);
		return;
	}
	m_protect.onDamaged(victim, attacker, amount);
	noteBaseDamage(victim, attacker, amount);
}

/// One of the objects of an ally took damage from an enemy (the body module reports it to the allies of the owner).
void AIStrategy::onAllyObjectDamaged( Object *victim, ObjectID attacker, Real amount )
{
	Player *owner = victim->getControllingPlayer();
	if (owner == nullptr || amount <= 0.0f)
		return;
	const Int idx = owner->getPlayerIndex();
	if (idx < 0 || idx >= MAX_PLAYER_COUNT || m_allyRadius[idx] <= 0.0f)
		return;
	if (dist2D(*victim->getPosition(), m_allyBase[idx]) > m_allyRadius[idx] + skill().m_allyHelpMargin)
		return;
	Object *source = TheGameLogic->findObjectByID(attacker);
	if (source != nullptr && m_player->getRelationship(source->getTeam()) != ENEMIES)
		return;
	m_allyDamageFrame[idx] = TheGameLogic->getFrame();
	// Where it comes from, when we can see it; else the place that was hit.
	m_allyDamagePos[idx] = (source && enemyCanSee(source)) ? *source->getPosition() : *victim->getPosition();
}

/// Armed enemy units seen in the base zone of the ally in the last seconds (or the place of the latest damage there).
Bool AIStrategy::findAllyThreat( Int idx, Coord3D *where, Real *value, Bool *hit ) const
{
	if (idx < 0 || idx >= MAX_PLAYER_COUNT || m_allyRadius[idx] <= 0.0f)
		return FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	const Real zone = m_allyRadius[idx] + sk.m_allyHelpMargin;
	Real threat = 0.0f, best = 0.0f;
	Coord3D at;
	at.zero();
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 3 * LOGICFRAMES_PER_SECOND)
			continue;
		if (dist2D(c.m_pos, m_allyBase[idx]) > zone)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed)
			continue;
		threat += f->m_cost;
		if (f->m_cost > best)
		{
			best = f->m_cost;
			at = c.m_pos;
		}
	}
	const Bool damaged = m_allyDamageFrame[idx] != 0 && now - m_allyDamageFrame[idx] <= 4 * LOGICFRAMES_PER_SECOND;
	if (idx == m_ahMutePlayer && now < m_ahMuteUntil && !damaged && threat < 2.0f * m_ahMuteValue)
		return FALSE;		// the force that stood about without hurting anything (see updateAllyHelp)
	if (threat < sk.m_allyHelpMinValue && !damaged)
		return FALSE;
	if (threat < sk.m_allyHelpMinValue)
	{
		at = m_allyDamagePos[idx];		// hit by something we do not see: go and look
		threat = sk.m_allyHelpMinValue;
	}
	*where = at;
	*value = threat;
	if (hit)
		*hit = damaged;
	return TRUE;
}

/// What already defends the ally's base: the armed units of the allies (not ours) in the zone and the allied defences that cover
/// the place of the threat; 'ours' gets our own armed units in the zone.
Real AIStrategy::allyDefenceNear( Int idx, const Coord3D &where, Real *ours ) const
{
	const Real zone = m_allyRadius[idx] + skill().m_allyHelpMargin;
	Real allies = 0.0f, own = 0.0f;
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&m_allyBase[idx], zone, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	for (Object *obj = iter->first(); obj; obj = iter->next())
	{
		if (obj->isEffectivelyDead() || obj->isOffMap() || obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
			continue;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		if (f == nullptr || !f->m_armed || !f->m_canHitGround)
			continue;
		if (obj->getControllingPlayer() == m_player)
		{
			if (!f->m_structure && !obj->isContained())
				own += f->m_cost;
			continue;
		}
		if (m_player->getRelationship(obj->getTeam()) != ALLIES)
			continue;
		if (f->m_structure)
		{
			if (dist2D(*obj->getPosition(), where) <= f->m_range + 60.0f)
				allies += f->m_cost;
		}
		else if (!obj->isContained() && !obj->isKindOf(KINDOF_DOZER) && !obj->isKindOf(KINDOF_HARVESTER))
		{
			allies += f->m_cost;
		}
	}
	if (ours)
		*ours = own;
	return allies;
}

/// Armed enemy units seen near our own base in the last 20 s (outside the zone of the ally that is attacked): what must stay at home.
Real AIStrategy::ownKnownThreat( const Coord3D &allyCenter, Real allyZone ) const
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return 0.0f;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real watch = m_ai->m_baseRadius + skill().m_baseDefenceMargin + HOME_WATCH;
	Real threat = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 20 * LOGICFRAMES_PER_SECOND)
			continue;
		if (dist2D(c.m_pos, base) > watch || dist2D(c.m_pos, allyCenter) <= allyZone)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f != nullptr && f->m_armed)
			threat += f->m_cost;
	}
	return threat;
}

/// Drops the teams that are no longer on the task (dead, retreating, taken by our own base defence ...).
void AIStrategy::pruneAllyHelpers()
{
	Int out = 0;
	for (Int i = 0; i < m_ahNumTeams; ++i)
	{
		const AITeamRecord *rec = findRecord(m_ahTeams[i], FALSE);
		Team *team = TheTeamFactory->findTeamByID(m_ahTeams[i]);
		if (rec == nullptr || team == nullptr || !team->hasAnyUnits() || rec->m_mode != AITEAM_DEFENDING || rec->m_baseDefence)
			continue;
		m_ahTeams[out++] = m_ahTeams[i];
	}
	m_ahNumTeams = out;
}

/// Frames of the current alarm in which teams were out at the ally's base.
UnsignedInt AIStrategy::allyHelpFrames( UnsignedInt now ) const
{
	return m_ahAlarmHelped + (m_ahHelpSince != 0 ? now - m_ahHelpSince : 0);
}

/// The teams that were sent go back to their role (the rally point, or the next wave).  Returns how many.
Int AIStrategy::releaseAllyHelpers()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	pruneAllyHelpers();
	Int released = 0;
	for (Int i = 0; i < m_ahNumTeams; ++i)
	{
		AITeamRecord *rec = findRecord(m_ahTeams[i], FALSE);
		if (rec == nullptr)
			continue;
		rec->m_mode = AITEAM_FREE;
		rec->m_modeFrame = now;
		rec->m_orderFrame = 0;
		++released;
	}
	m_ahNumTeams = 0;
	if (m_ahHelpSince != 0)
	{
		m_ahAlarmHelped += now - m_ahHelpSince;
		m_ahHelpSince = 0;
	}
	return released;
}

/// The alarm is over: the teams that were sent go back to their role (the rally point, or the next wave).
void AIStrategy::endAllyHelp( const char *why )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Int released = releaseAllyHelpers();
	const UnsignedInt helped = allyHelpFrames(now);
	m_ahHelpFrames += helped;
	AI_TRACE("ALLYHELP clear after %u s: %d team(s) go back to their role (player %d, helped %u s; %s)", (now - m_ahSince) / LOGICFRAMES_PER_SECOND, released, m_ahPlayer,
		helped / LOGICFRAMES_PER_SECOND, why);
	m_ahNumTeams = 0;
	m_ahActive = FALSE;
	m_ahHelpSince = 0;
	m_ahAlarmHelped = 0;
	m_ahPlayer = -1;
}

void AIStrategy::updateAllyHelp()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextAllyHelp)
		return;
	m_nextAllyHelp = now + LOGICFRAMES_PER_SECOND;
	refreshAllyBases();

	const AISkillSettings &sk = skill();
	const Bool respond = allyHelpOn();

	// Our own base first: no help while it has an alarm, and help that is out comes back.
	if (m_bdActive)
	{
		if (m_ahActive)
			endAllyHelp("our own base is under attack");
		return;
	}

	Coord3D base;
	const Bool haveBase = m_ai->getBaseCenter(&base);

	// Which ally needs help: the one with the largest threat, the nearer the better.  (The one that is helped stays chosen until its base is clear.)
	Coord3D where;
	Real value = 0.0f;
	Bool hit = FALSE;
	Bool threat = FALSE;
	if (m_ahActive)
	{
		threat = findAllyThreat(m_ahPlayer, &where, &value, &hit);
	}
	else
	{
		Real bestScore = 0.0f;
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			Coord3D w;
			Real v = 0.0f;
			Bool h = FALSE;
			if (!findAllyThreat(i, &w, &v, &h))
				continue;
			const Real d = haveBase ? dist2D(base, m_allyBase[i]) : 0.0f;
			const Real score = v / (1.0f + d / (sk.m_allyHelpMaxDistance > 1.0f ? sk.m_allyHelpMaxDistance : 1.0f));
			if (score > bestScore)
			{
				bestScore = score;
				m_ahPlayer = i;
				where = w;
				value = v;
				hit = h;
				threat = TRUE;
			}
		}
	}

	if (!threat)
	{
		if (m_ahActive && now - m_ahLastThreat >= secondsToFrames(sk.m_allyHelpClearSeconds))
			endAllyHelp("the base is clear");
		return;
	}

	// An alarm that has gone on for long without anything of the ally being hit is over (a force that watches the base from afar).
	if (m_ahActive && now - m_ahSince >= secondsToFrames(sk.m_allyHelpMaxSeconds) &&
		(m_allyDamageFrame[m_ahPlayer] == 0 || now - m_allyDamageFrame[m_ahPlayer] > 15 * LOGICFRAMES_PER_SECOND))
	{
		m_ahMutePlayer = m_ahPlayer;
		m_ahMuteUntil = now + 90 * LOGICFRAMES_PER_SECOND;
		m_ahMuteValue = value;
		endAllyHelp("stale: nothing of the ally was hit for 15 s; the force is ignored for 90 s unless it hits something");
		return;
	}

	const Int idx = m_ahPlayer;
	const Real zone = m_allyRadius[idx] + sk.m_allyHelpMargin;
	m_ahLastThreat = now;
	m_ahPos = where;
	m_ahValue = value;
	if (!m_ahActive)
	{
		m_ahActive = TRUE;
		m_ahSince = now;
		m_ahHelpSince = 0;
		m_ahAlarmHelped = 0;
		m_ahNoteFrame = 0;
		m_ahLastSend = 0;
		++m_ahAlarms;
		AI_TRACE("ALLYHELP alarm %d: player %d base at (%.0f,%.0f) radius %.0f attacked by %.0f at (%.0f,%.0f)%s, %.0f from our base; response %s",
			m_ahAlarms, idx, m_allyBase[idx].x, m_allyBase[idx].y, m_allyRadius[idx], value, where.x, where.y, hit ? " (its objects are hit)" : "",
			haveBase ? dist2D(base, m_allyBase[idx]) : 0.0f, respond ? "on" : "OFF");
	}
	// The force that the help is sized against: the largest one seen in the last 20 s (what is in sight in the ally's base changes from second to
	// second, and help that is sent against a part of the force and withdrawn when the rest comes into sight is wasted).
	if (value >= m_ahPeak || now - m_ahPeakFrame > 20 * LOGICFRAMES_PER_SECOND || now == m_ahSince)
	{
		m_ahPeak = value;
		m_ahPeakFrame = now;
	}
	value = m_ahPeak;
	if (!respond)
		return;

	// The teams that are there follow the threat when it has moved on.  (The time helping stops while none is out.)
	pruneAllyHelpers();
	if (m_ahNumTeams == 0 && m_ahHelpSince != 0)
	{
		m_ahAlarmHelped += now - m_ahHelpSince;
		m_ahHelpSince = 0;
	}
	for (Int i = 0; i < m_ahNumTeams; ++i)
	{
		AITeamRecord *rec = findRecord(m_ahTeams[i], FALSE);
		Team *team = TheTeamFactory->findTeamByID(m_ahTeams[i]);
		if (rec == nullptr || team == nullptr)
			continue;
		if (dist2D(rec->m_target, where) > 200.0f && now - rec->m_orderFrame >= 3 * LOGICFRAMES_PER_SECOND)
		{
			rec->m_target = where;
			rec->m_orderFrame = now;
			orderTeamAttackMove(team, &where);
		}
	}

	// How much is missing: the force the threat needs, less what defends the place already.
	if (now - m_ahLastSend < 5 * LOGICFRAMES_PER_SECOND && m_ahLastSend != 0)
		return;
	Real ours = 0.0f;
	const Real defenders = allyDefenceNear(idx, where, &ours);
	// The teams that are on their way count as there.
	Real onTheWay = 0.0f;
	for (Int i = 0; i < m_ahNumTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_ahTeams[i]);
		if (team != nullptr)
			onTheWay += teamValue(team);
	}
	if (onTheWay > ours)
		ours = onTheWay;
	const Real need = sk.m_allyHelpForce * value - defenders - ours;
	if (need <= 0.0f)
	{
		if (m_ahNumTeams == 0 && now - m_ahNoteFrame >= 15 * LOGICFRAMES_PER_SECOND)
		{
			m_ahNoteFrame = now;
			AI_TRACE("ALLYHELP: player %d holds its base against %.0f (its own and the allies' %.0f, ours %.0f there): no help needed", idx, value, defenders, ours);
		}
		return;
	}

	// The candidates: teams at home, and teams of a wave that are near the ally's base; the nearest first.
	enum { MAX_CAND = 24 };
	Int cand[MAX_CAND];
	Real candDist[MAX_CAND];
	Bool candHome[MAX_CAND];
	Int numCand = 0;
	Real homeValue = 0.0f;
	Int homeTeams = 0;
	const Real homeReach = m_ai->m_baseRadius + 250.0f + 600.0f;
	for (Int i = 0; i < m_numTeams && numCand < MAX_CAND; ++i)
	{
		AITeamRecord &rec = m_teams[i];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr || !isManageableTeam(team) || rec.m_mode == AITEAM_RETREATING || rec.m_baseDefence || isAllyHelper(rec.m_team))
			continue;
		const Coord3D *p = team->getEstimateTeamPosition();
		if (p == nullptr)
			continue;
		const Bool home = haveBase && dist2D(*p, base) <= homeReach;
		const Real d = dist2D(*p, where);
		if (home)
		{
			homeValue += teamValue(team);
			++homeTeams;
		}
		const Bool nearWave = !home && rec.m_mode == AITEAM_ATTACKING && d <= sk.m_allyHelpWaveReach;
		if ((!home && !nearWave) || d > sk.m_allyHelpMaxDistance || teamValue(team) <= 0.0f)
			continue;
		// Insertion by distance (ties: the order of the records).
		Int at = numCand;
		while (at > 0 && candDist[at - 1] > d)
		{
			cand[at] = cand[at - 1];
			candDist[at] = candDist[at - 1];
			candHome[at] = candHome[at - 1];
			--at;
		}
		cand[at] = i;
		candDist[at] = d;
		candHome[at] = home;
		++numCand;
	}

	// What stays at home: at least the known threat near our base, and then one team too.
	const Real known = ownKnownThreat(m_allyBase[idx], zone);
	const Real keep = sk.m_allyHelpHomeGuard * known;
	Int pick[MAX_CAND];
	Int numPick = 0, fromHome = 0, fromWave = 0;
	Real sent = 0.0f, homeLeft = homeValue;
	Int homeTeamsLeft = homeTeams;
	// One team that is enough on its own, the smallest of those that are not much farther than the nearest: the rest of the army stays.
	Int single = -1;
	Real singleValue = 0.0f;
	for (Int c = 0; c < numCand; ++c)
	{
		const Real v = teamValue(TheTeamFactory->findTeamByID(m_teams[cand[c]].m_team));
		if (v < need || candDist[c] > candDist[0] + 600.0f || (single >= 0 && v >= singleValue))
			continue;
		if (candHome[c] && (homeLeft - v < keep || (known > 0.0f && homeTeamsLeft <= 1)))
			continue;
		single = c;
		singleValue = v;
	}
	for (Int c = 0; c < numCand && sent < need; ++c)
	{
		if (single >= 0 && c != single)
			continue;
		Team *team = TheTeamFactory->findTeamByID(m_teams[cand[c]].m_team);
		const Real v = teamValue(team);
		if (candHome[c])
		{
			if (homeLeft - v < keep || (known > 0.0f && homeTeamsLeft <= 1))
				continue;
			homeLeft -= v;
			--homeTeamsLeft;
			++fromHome;
		}
		else
		{
			++fromWave;
		}
		pick[numPick++] = cand[c];
		sent += v;
	}
	if (defenders + ours + sent < sk.m_allyHelpMinAdvantage * value)
	{
		// The fight there is lost, and nothing more can be sent: the teams that are there come back (they would be lost, and the waves wait for them).
		if (m_ahNumTeams > 0)
		{
			const Int back = releaseAllyHelpers();
			AI_TRACE("ALLYHELP: %d team(s) withdraw from player %d base: the threat of %.0f is too strong (the defenders %.0f, ours there %.0f, %.0f more could be sent)",
				back, idx, value, defenders, ours, sent);
			m_ahNoteFrame = now;
			return;
		}
	}
	if (numPick == 0 || defenders + ours + sent < sk.m_allyHelpMinAdvantage * value)
	{
		if (now - m_ahNoteFrame >= 15 * LOGICFRAMES_PER_SECOND)
		{
			m_ahNoteFrame = now;
			AI_TRACE("ALLYHELP: no help for player %d against %.0f: %d team(s) in reach could bring %.0f (with the defenders %.0f, ours there %.0f); own home %.0f keeps %.0f (enemies seen near our base %.0f)",
				idx, value, numPick, sent, defenders, ours, homeValue, keep, known);
		}
		return;
	}

	Coord3D from;
	from.zero();
	for (Int c = 0; c < numPick; ++c)
	{
		AITeamRecord &rec = m_teams[pick[c]];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		const Coord3D *p = team->getEstimateTeamPosition();
		if (p)
		{
			from.x += p->x / numPick;
			from.y += p->y / numPick;
		}
		rec.m_mode = AITEAM_DEFENDING;
		rec.m_modeFrame = now;
		rec.m_orderFrame = now;
		rec.m_target = where;
		rec.m_idleSince = 0;
		rec.m_badSince = 0;
		rec.m_inWave = FALSE;		// it is not part of the wave any more; afterwards it joins the next one
		orderTeamAttackMove(team, &where);
		if (m_ahNumTeams < MAX_HELP_TEAMS)
			m_ahTeams[m_ahNumTeams++] = rec.m_team;
	}
	m_ahLastSend = now;
	if (m_ahHelpSince == 0)
		m_ahHelpSince = now;
	++m_ahOrders;
	m_ahTeamsSent += numPick;
	AI_TRACE("ALLYHELP: %d team(s) to player %d base at (%.0f,%.0f) against %.0f (own home %.0f): value %.0f from (%.0f,%.0f), %d from home and %d from the wave, %.0f away; need %.0f (the ally's defenders %.0f, ours there %.0f); %.0f stays home (enemies seen near our base %.0f)",
		numPick, idx, where.x, where.y, value, homeValue, sent, from.x, from.y, fromHome, fromWave, dist2D(from, where), need, defenders, ours, homeLeft, known);
}
