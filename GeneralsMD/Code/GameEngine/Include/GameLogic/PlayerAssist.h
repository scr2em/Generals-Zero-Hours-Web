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
#include "GameLogic/AITacticsCore.h"
#include "GameLogic/Protect.h"

class AIGroup;
class GameMessage;
class Xfer;
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

/// The unit stances the player can switch on.  The values are part of the network messages.
enum AssistStanceBit CPP_11(: Int)
{
	STANCE_KITE = 1,				///< step back while the weapon reloads
	STANCE_RETREAT = 2,			///< pull out when damaged (the percent of health is a setting)
	STANCE_SPREAD = 4,			///< keep apart from friends against area weapons
	STANCE_SPLIT = 8				///< do not shoot at a target that has enough fire on it already
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
	/// Test aid (AssistTest.cpp): create the units of one "-assistTest" entry (player:template:count[:ref:dx:dy]) now.
	/// 'origin', when given, is where the first unit stands instead of the base of "ref" shifted by dx, dy.  The ids of
	/// the new units are added to 'created' when given.  Returns the number of units created.
	static Int createTestUnits( const char *entry, const Coord3D *origin, std::vector<ObjectID> *created, Int atFrame = -1 );

	/// A command of the player arrived (from the logic message dispatcher).  'group' is the player's selection.
	Bool onMessage( GameMessage *msg, Player *player, AIGroup *group );

	// ---- protect (Protect.h) -------------------------------------------------------------------
	const ProtectManager &protect() const { return m_protect; }
	/// The selection was moved by hand to 'dest' (a move order of the player): the homes of protectors in it move there.
	void noteGroupMove( AIGroup *group, const Coord3D *dest );

	// ---- base under attack (AssistBaseAlert.cpp) -------------------------------------------------
	/// A response is out for this player: units were sent to defend and not yet called back.
	Bool defendActive( Int playerIndex ) const;
	void baseDefend( Player *player, const Coord3D &where );
	void baseReturn( Player *player );

	// ---- unit stances (AssistStance.cpp) ---------------------------------------------------------
	/// The stance bits set for this unit (0: none).
	Int stanceOf( ObjectID id ) const;
	Int retreatPercentOf( ObjectID id ) const;
	/// The bits that all eligible units of the list share; 'retreatPercent' gets the retreat setting they share (0: off or mixed).
	Int sharedStance( const std::vector<ObjectID> &ids, Int *retreatPercent ) const;
	/// A unit a stance makes sense for: armed, mobile, on the ground.
	static Bool stanceEligible( const Object *obj );

	// ---- idle units (AssistIdle.cpp): questions for the idle hotkeys and the idle counter, no orders ------------------
	enum IdleKind { IDLE_ARMY = 0, IDLE_WORKER = 1 };
	/// A unit that can shoot, is not a worker, harvester or support unit nor a structure, and can be ordered about (the
	/// definition of the "base under attack" response as well).
	static Bool isArmyUnit( const Object *obj );
	/// A builder (dozer, worker) or a supply gatherer.
	static Bool isWorker( const Object *obj );
	/// An army unit (IDLE_ARMY) or a worker (IDLE_WORKER) that stands idle: its AI is idle and, for a worker, it has no
	/// building job pending and is not on a supply round.
	static Bool isIdleUnit( Object *obj, Int kind );
	/// The player's idle units of that kind, ordered by object id.
	static void idleUnits( const Player *player, Int kind, std::vector<Object *> &out );
	enum IdlePick { IDLE_PICK_ARMY_NEXT = 0, IDLE_PICK_ARMY_ALL, IDLE_PICK_WORKER_NEXT };
	/// What an idle hotkey selects: the ids go to 'out' (empty when nothing is idle).  "Next" is the idle unit with the
	/// smallest id above 'after' (the unit the hotkey picked last time), or the first one again.  Prints the ASSIST line of the pick (-assistDebug).  Used by the hotkeys and the test bench.
	static void pickIdle( const Player *player, Int pick, ObjectID after, std::vector<ObjectID> &out );

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

	/// Idle army units that were sent to the attacked place, and where they stood.
	struct DefendState
	{
		Bool									m_active;
		Coord3D								m_location;
		UnsignedInt						m_frame;
		std::vector<ObjectID>	m_units;
		std::vector<Coord3D>	m_origins;
		DefendState() : m_active( FALSE ), m_frame( 0 ) { m_location.x = m_location.y = m_location.z = 0.0f; }
	};
	enum { MAX_DEFEND = 16 };

	// the state of the stances of one unit
	enum { PHASE_NONE = 0, PHASE_STEP, PHASE_ATTACK, PHASE_RETREAT, PHASE_PARKED };
	struct StanceState
	{
		UnsignedByte	m_flags;						///< STANCE_ bits
		UnsignedByte	m_retreatPercent;		///< retreat when below this share of the health
		UnsignedByte	m_phase;						///< PHASE_
		ObjectID			m_victim;						///< the enemy it steps back from
		ObjectID			m_facility;					///< the place of repair it goes to
		UnsignedInt		m_until;						///< end of the phase
		Coord3D				m_from;							///< where it stood when it pulled out
		Coord3D				m_to;								///< the place it pulls out to (no facility)
		Real					m_spacing;					///< distance kept to friends against area weapons (0: not needed)
		UnsignedInt		m_spacingUntil;
		UnsignedInt		m_noRetreatUntil;		///< the player sent it back: no retreat until then
		ObjectID			m_splitTarget;			///< the target this unit has put on the split fire ledger
		Real					m_splitDamage;
		UnsignedInt		m_splitUntil;
		StanceState() : m_flags( 0 ), m_retreatPercent( 0 ), m_phase( 0 ), m_victim( INVALID_ID ), m_facility( INVALID_ID ), m_until( 0 ),
			m_spacing( 0.0f ), m_spacingUntil( 0 ), m_noRetreatUntil( 0 ), m_splitTarget( INVALID_ID ), m_splitDamage( 0.0f ), m_splitUntil( 0 )
		{
			m_from.x = m_from.y = m_from.z = 0.0f;
			m_to = m_from;
		}
	};
	typedef std::map<ObjectID, StanceState> StanceMap;
	enum { MAX_LEDGERS = 16 };

	void setStance( Player *player, AIGroup *group, Int mask, Int value, Int percent );
	void updateStances( UnsignedInt now );
	void stanceUnit( Object *unit, StanceState &st, UnsignedInt now );
	void startRetreat( Object *unit, StanceState &st, UnsignedInt now );
	void endRetreat( Object *unit, StanceState &st );
	Object *pickOtherTarget( Object *unit, Object *current, Real range, AITactics::SplitLedger &ledger, UnsignedInt now, const AITactics::Params &params );
	void xferStances( Xfer *xfer );

	void setFormation( AIGroup *group, Int type );
	void formationMove( AIGroup *group, Int type, const Coord3D &a, const Coord3D &b, Bool attackMove );
	void pruneDead();
	Int roleOf( Object *obj, Real maxRange, Real maxHealth, Real *key ) const;

	Bool				m_allowed;
	UnitMap			m_units;
	ProtectManager	m_protect;
	DefendState			m_defend[MAX_DEFEND];
	StanceMap			m_stances;
	AITactics::SplitLedger	m_ledgers[MAX_LEDGERS];			///< split fire: per player, damage assigned to targets

	// the drag a formation move was given (valid only while the command is being carried out)
	Bool				m_aimValid;
	Real				m_aimAngle;
	Real				m_aimWidth;
	UnsignedInt	m_pruneFrame;
};

extern PlayerAssist *ThePlayerAssist;
