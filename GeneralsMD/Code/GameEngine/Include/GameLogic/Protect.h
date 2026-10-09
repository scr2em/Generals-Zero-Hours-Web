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

// Protect.h
// Protect links: a unit (the protector) watches over other units or buildings (the protected).  The protector stays
// at its home position.  When a protected object takes damage from an enemy and the place is within the leash
// radius of the home, the protector attack-moves there and fights.  It returns home when no enemy is left near
// it or the protected, or when the fight takes too long or leads too far from home.  When the protected group
// moves, the home of the protector moves with it (it escorts, keeping its offset).
//
// The module knows nothing about the user interface: the player assists drive it from commands of the player, and
// the computer player can use the same links.  It is part of the simulation: it reads synchronised state only, keeps its
// links in a map ordered by object id, spreads its work over the logic frames and is saved with the game.

#pragma once

#include <map>
#include <vector>

#include "Common/GameCommon.h"
#include "Common/Snapshot.h"

class Object;
class Player;
class Xfer;

enum ProtectState CPP_11(: Int)
{
	PROTECT_AT_HOME = 0,		///< on station (or on the way there)
	PROTECT_RESPONDING,			///< fighting for the protected
	PROTECT_RETURNING				///< on the way home after a fight
};

struct ProtectLink
{
	ObjectID					m_protector;
	Short							m_player;					///< owner of the protector (index in the player list)
	Short							m_squad;					///< hotkey group whose members are protected as well (-1 none): new members join
	std::vector<ObjectID>	m_targets;		///< protected objects (sorted)
	Coord3D						m_home;						///< where the protector stays
	Coord2D						m_offset;					///< home minus the centre of the protected: the home follows a protected group that moves
	Coord3D						m_lastCentre;			///< centre of the protected when the home was last moved
	Bool							m_hasOffset;
	UnsignedByte			m_state;					///< ProtectState
	UnsignedInt				m_stateFrame;			///< when the state was entered
	UnsignedInt				m_lastOrderFrame;	///< frame of the last order the link gave
	UnsignedInt				m_lastFightFrame;	///< frame at which an enemy was last near
	Coord3D						m_alert;					///< where the last alarm came from

	ProtectLink() : m_protector( INVALID_ID ), m_player( 0 ), m_squad( -1 ), m_hasOffset( FALSE ), m_state( 0 ),
		m_stateFrame( 0 ), m_lastOrderFrame( 0 ), m_lastFightFrame( 0 )
	{
		m_home.x = m_home.y = m_home.z = 0.0f;
		m_offset.x = m_offset.y = 0.0f;
		m_lastCentre = m_home;
		m_alert = m_home;
	}
};

class ProtectManager
{
public:
	/// Tuning, in world units and frames.
	struct Params
	{
		Real					m_responseRadius;			///< an alarm within this distance of the home is answered
		Real					m_fightRadius;				///< enemies this close to the protector or a protected object keep the fight going
		Real					m_leashDistance;			///< the protector goes home when it is further from home than this
		UnsignedInt		m_leashFrames;				///< ... or when it has been fighting this long
		UnsignedInt		m_quietFrames;				///< ... or when no enemy has been near for this long
		Real					m_homeTolerance;			///< closer than this to home counts as home
		Real					m_followDistance;			///< the protected must move this far before the homes follow
	};
	static const Params &params();

	ProtectManager();
	void reset();

	/// Protect: 'protector' takes the objects 'targets' (and the members of hotkey group 'squad' when >= 0) under its wing.
	/// The protector's home is where it stands now (or 'home' when given).  Returns false if nothing could be linked.
	Bool link( Object *protector, const std::vector<ObjectID> &targets, Int squad, const Coord3D *home );
	Bool unlink( ObjectID protector );
	const ProtectLink *find( ObjectID protector ) const;
	Bool isProtector( ObjectID protector ) const { return find( protector ) != nullptr; }
	Int count() const { return (Int)m_links.size(); }

	/// The objects the protector protects now: the explicit list and the live members of the hotkey group, without the protector itself.
	void resolveTargets( const ProtectLink &link, std::vector<ObjectID> &out ) const;

	/// The player moved the protector by hand: its home is where it was sent.
	void noteManualMove( ObjectID protector, const Coord3D &dest );

	/// Once per logic frame.
	void update( UnsignedInt frame );

	void xfer( Xfer *xfer );

private:
	typedef std::map<ObjectID, ProtectLink> LinkMap;

	void updateLink( ProtectLink &link, Object *protector, UnsignedInt frame );
	void scanAlarms( UnsignedInt frame );
	Bool centreOf( const std::vector<ObjectID> &targets, Coord3D *centre ) const;
	void goHome( ProtectLink &link, Object *protector, UnsignedInt frame );

	LinkMap										m_links;
	std::map<ObjectID, UnsignedInt>		m_damageSeen;		///< protected object -> frame of the damage that was handled
	UnsignedInt								m_nextScan;
};
