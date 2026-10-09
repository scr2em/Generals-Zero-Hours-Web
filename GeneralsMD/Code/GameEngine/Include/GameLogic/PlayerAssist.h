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

// PlayerAssist.h
// Optional helpers for the human player that issue orders: formations, protect links, unit stances,
// repeat production, "base under attack" responses.
//
// Everything in here is part of the simulation.  It is driven only by game messages (so it goes through the
// message stream, the network and the replay like any other command) and by the logic frame update, reads only
// synchronised game state, never uses the wall clock, uses GameLogicRandomValue for chance, and keeps its
// memory in containers keyed by object id (ordered) so that every client computes the same thing.  The state is
// saved with the game and part of the logic CRC.
//
// The helpers only act when the match allows them (a game setting every player sees, see
// GameInfo::getPlayerAssistsAllowed); a command that arrives while they are not allowed is ignored.

#pragma once

#include <map>
#include <vector>

#include "Common/GameCommon.h"
#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"

class AIGroup;
class GameMessage;
class Object;
class Player;

//-------------------------------------------------------------------------------------------------
/// The formations that the player can pick.  The values are part of the network messages.
enum AssistFormation CPP_11(: Int)
{
	AFORM_NONE = 0,			///< no formation: the units move as the game always did
	AFORM_LINE,					///< a front line, facing the way the group moves
	AFORM_COLUMN,				///< a narrow column, as for a road
	AFORM_WEDGE,				///< a triangle with the tip in front
	AFORM_BOX,					///< a compact block
	AFORM_LOOSE,				///< spread out, so that area damage hits few units
	AFORM_KEEP,					///< keep the shape the group has now (the game's own formation)
	AFORM_COUNT
};

/// How a unit is placed in a formation: front rows take the hits, middle rows protect the support units, back rows shoot over them.
enum AssistRole CPP_11(: Int)
{
	AROLE_FRONT = 0,
	AROLE_MIDDLE,
	AROLE_BACK
};

//-------------------------------------------------------------------------------------------------
/// One slot of a formation, in the frame of the formation: x to the left of the heading, depth behind the front row.
struct AssistSlot
{
	Real	m_x;
	Real	m_depth;
	Int		m_row;
};

//-------------------------------------------------------------------------------------------------
class PlayerAssist : public SubsystemInterface, public Snapshot
{
public:
	PlayerAssist();
	virtual ~PlayerAssist() override;

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override {}

	/// Called when a match starts (not when a saved game is loaded: the setting is in the save).
	void startMatch( Bool allowed );
	Bool allowed() const { return m_allowed; }

	/// Once per logic frame, after the objects have been updated.
	void logicUpdate();

	/// Test aid (AssistTest.cpp): create the units of "-assistTest" in the first frames of a match.
	void runTestSpec();

	/// A command of the player arrived (from the logic message dispatcher).  'group' is the player's selection.
	Bool onMessage( GameMessage *msg, Player *player, AIGroup *group );

	// ---- formations (AssistFormation.cpp) ------------------------------------------------------
	/// The formation set for this unit (AFORM_NONE if it has none).
	Int formationOf( ObjectID id ) const;
	/// The formation that all mobile members of the list share, or AFORM_NONE when they differ.
	Int sharedFormation( const std::vector<ObjectID> &ids ) const;

	/// Called from AIGroup::groupMoveToPosition: if the group has one of the layout formations, set up each unit's
	/// formation offset for a move to 'dest' (heading and width from a drag if one is pending) and return true.
	Bool prepareFormationMove( AIGroup *group, const Coord3D *dest );

	/// A hotkey group was created from these units: they share one formation from now on.
	void onTeamCreated( const std::vector<Object *> &members );

	/// The slots of a formation of 'count' units (a pure function; also used by the tests).
	static void layoutSlots( Int type, Int count, Real spacing, Real depthSpacing, Real width, std::vector<AssistSlot> &out );

	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:
	struct UnitState
	{
		UnsignedByte	m_formation;
	};
	typedef std::map<ObjectID, UnitState> UnitMap;			// ordered by object id: deterministic iteration

	void setFormation( AIGroup *group, Int type );
	void formationMove( AIGroup *group, Int type, const Coord3D &a, const Coord3D &b, Bool attackMove );
	void pruneDead();
	Int roleOf( Object *obj, Real maxRange, Real maxHealth, Real *key ) const;

	Bool				m_allowed;
	UnitMap			m_units;

	// the drag a formation move was given (valid only while the command is being carried out)
	Bool				m_aimValid;
	Real				m_aimAngle;
	Real				m_aimWidth;
	UnsignedInt	m_pruneFrame;
};

extern PlayerAssist *ThePlayerAssist;
