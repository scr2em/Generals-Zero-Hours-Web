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

// AITunnel.cpp
// Tunnel assault for the Expert computer player.
//
// A sneak attack (the power of the scripts) opens a tunnel of ours far from our base, in the enemy's.  Units only come out of it when
// they are in the tunnel network, and the computer player never put any in.  The Expert does:
//   * every two seconds it looks for a tunnel of ours far from the base (a forward tunnel) and one at home,
//   * armed ground units of the teams at home (not on a wave, not on a task of their own) walk into the tunnel at home, as many as
//     the network has room for,
//   * when they are in (or after 40 s, with the ones that are), the forward tunnel lets the whole network out,
//   * the units that come out leave their teams and seek and destroy in the enemy base,
//   * while the forward tunnel stands, the next group goes 90 s later.
// The tunnels are known by their contain module (isTunnelContain), no names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/TunnelTracker.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Object.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::isTunnelSquad( ObjectID id ) const
{
	for (Int i = 0; i < m_numTunnelSquad; ++i)
		if (m_tunnelSquad[i] == id)
			return TRUE;
	return FALSE;
}

void AIStrategy::endTunnelMission( const char *why )
{
	AI_TRACE("TUNNEL: mission over (%s)", why);
	m_numTunnelSquad = 0;
	m_tunnelPhase = 0;
	m_tunnelCooldown = TheGameLogic->getFrame() + 90 * LOGICFRAMES_PER_SECOND;
}

void AIStrategy::updateTunnelAssault()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!skill().m_useTunnelAssault || now < m_nextTunnel)
		return;
	m_nextTunnel = now + 2 * LOGICFRAMES_PER_SECOND;
	Coord3D base;
	TunnelTracker *net = m_player->getTunnelSystem();
	if (net == nullptr || !m_ai->getBaseCenter(&base))
		return;

	// Our tunnels: the one nearest to the base, and the one farthest out (beyond the base and a margin).
	m_tunnelsSeen = 0;
	m_tunnelFarthest = 0.0f;
	Object *home = nullptr, *forward = nullptr;
	Real homeDist = 0.0f, forwardDist = 0.0f;
	const Real outer = m_ai->m_baseRadius + 800.0f;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE) || obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					continue;
				ContainModuleInterface *contain = obj->getContain();
				if (contain == nullptr || !contain->isTunnelContain())
					continue;
				const Real d = dist2D(*obj->getPosition(), base);
				++m_tunnelsSeen;
				if (d > m_tunnelFarthest)
					m_tunnelFarthest = d;
				if (d > outer)
				{
					if (forward == nullptr || d > forwardDist)
					{
						forward = obj;
						forwardDist = d;
					}
				}
				else if (home == nullptr || d < homeDist)
				{
					home = obj;
					homeDist = d;
				}
			}
		}
	}
	if (forward == nullptr)
	{
		if (m_tunnelPhase != 0)
			endTunnelMission("the forward tunnel is gone");
		return;
	}

	if (m_tunnelPhase == 0)
	{
		if (home == nullptr || now < m_tunnelCooldown)
			return;
		// The group: armed ground units of the teams at home that the network takes.
		Int room = net->getContainMax() - (Int)net->getContainCount();
		if (room > MAX_TUNNEL_SQUAD)
			room = MAX_TUNNEL_SQUAD;
		Object *picks[MAX_TUNNEL_SQUAD];
		Int numPicks = 0;
		for (Int t = 0; t < m_numTeams && numPicks < room; ++t)
		{
			const AITeamRecord &rec = m_teams[t];
			Team *team = TheTeamFactory->findTeamByID(rec.m_team);
			const Coord3D *p = team ? team->getEstimateTeamPosition() : nullptr;
			if (p == nullptr || rec.m_inWave || rec.m_mode != AITEAM_FREE || dist2D(*p, base) > m_ai->m_baseRadius + 600.0f)
				continue;
			for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done() && numPicks < room; it.advance())
			{
				Object *obj = it.cur();
				const AICombatFigures *f = obj ? AICombatModel::figures(obj->getTemplate()) : nullptr;
				if (f == nullptr || !f->m_armed || f->m_airborne || obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == nullptr ||
						isDetached(obj->getID()) || !net->isValidContainerFor(obj, TRUE))
					continue;
				picks[numPicks++] = obj;
			}
		}
		if (numPicks < 2)
			return;
		for (Int i = 0; i < numPicks; ++i)
		{
			m_tunnelSquad[i] = picks[i]->getID();
			picks[i]->getAI()->aiEnter(home, CMD_FROM_AI);
		}
		m_numTunnelSquad = numPicks;
		m_tunnelPhase = 1;
		m_tunnelStart = now;
		AI_TRACE("TUNNEL: forward tunnel %u at (%.0f,%.0f): %d unit(s) go into the tunnel %u at home", forward->getID(), forward->getPosition()->x,
			forward->getPosition()->y, numPicks, home->getID());
		return;
	}

	// The group: who lives, who is in the network.
	Int alive = 0, inside = 0, out = 0;
	for (Int i = 0; i < m_numTunnelSquad; ++i)
	{
		Object *obj = TheGameLogic->findObjectByID(m_tunnelSquad[i]);
		if (obj == nullptr || obj->isEffectivelyDead())
			continue;
		m_tunnelSquad[out++] = m_tunnelSquad[i];
		++alive;
		if (net->isInContainer(obj))
			++inside;
	}
	m_numTunnelSquad = out;
	if (alive == 0)
	{
		endTunnelMission("the group is lost");
		return;
	}

	if (m_tunnelPhase == 1)
	{
		const Bool waitedLong = now - m_tunnelStart > 40 * LOGICFRAMES_PER_SECOND;
		if (inside == alive || (waitedLong && inside > 0))
		{
			forward->getContain()->orderAllPassengersToExit(CMD_FROM_AI, FALSE);
			m_tunnelPhase = 2;
			m_tunnelStart = now;
			++m_tunnelDrops;
			AI_TRACE("TUNNEL: %d unit(s) come out of the forward tunnel %u", inside, forward->getID());
		}
		else if (now - m_tunnelStart > 60 * LOGICFRAMES_PER_SECOND)
		{
			endTunnelMission("the group did not get into the tunnel");
		}
		return;
	}

	// Out: they leave their teams and seek and destroy in the enemy base.
	if (now - m_tunnelStart < 4 * LOGICFRAMES_PER_SECOND)
		return;
	Int sent = 0;
	for (Int i = 0; i < m_numTunnelSquad; ++i)
	{
		Object *obj = TheGameLogic->findObjectByID(m_tunnelSquad[i]);
		if (obj == nullptr || obj->isEffectivelyDead() || net->isInContainer(obj) || obj->getAI() == nullptr)
			continue;
		if (obj->getTeam() == m_player->getDefaultTeam())
			continue;	// sent already
		obj->setTeam(m_player->getDefaultTeam());
		obj->getAI()->aiHunt(CMD_FROM_AI);
		++sent;
	}
	if (inside == 0 || now - m_tunnelStart > 20 * LOGICFRAMES_PER_SECOND)
	{
		AI_TRACE("TUNNEL: %d unit(s) attack in the enemy base", sent);
		endTunnelMission("the group is out");
	}
}
